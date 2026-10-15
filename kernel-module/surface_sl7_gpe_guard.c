// SPDX-License-Identifier: GPL-2.0-only
/* Experimental containment of a broken GPE, limited to the observed firmware. */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>

#define BAD_GPE 0xAB
static bool enable_trial;
module_param(enable_trial, bool, 0444);
MODULE_PARM_DESC(enable_trial, "Explicitly enable the experimental SL7 GPE guard");
static unsigned long suppressed_events;
module_param(suppressed_events, ulong, 0444);
static bool installed;
static bool quarantine_bad_io;
module_param(quarantine_bad_io, bool, 0444);
MODULE_PARM_DESC(quarantine_bad_io, "Opt in to bounded SCI quarantine on invalid register reads");
static unsigned int sci_irq;
static atomic_t sci_held = ATOMIC_INIT(0);
static bool exiting;
static int get_sci_paused(char *buffer, const struct kernel_param *kp)
{
    return scnprintf(buffer, 4, "%d\n", atomic_read(&sci_held));
}
static const struct kernel_param_ops sci_paused_ops = { .get = get_sci_paused };
module_param_cb(sci_paused, &sci_paused_ops, NULL, 0444);

static struct timer_list io_ready_timer;
static unsigned long release_deadline;
static unsigned long quarantine_count, quarantine_timeouts;
module_param(quarantine_count, ulong, 0444);
module_param(quarantine_timeouts, ulong, 0444);

/* Read-only telemetry for the GPE register layout of BIOS 18.106.143.
 * Bits 0..7: GPE20-27 status, 8..15: enable; 16..23: GPEA8-AF
 * status, 24..31: enable. Standard dispatcher has already tried to
 * disable AB before the callback; all-ones enables would be unexpected.
 */
static unsigned int first_register_sample, last_register_sample;
module_param(first_register_sample, uint, 0444);
module_param(last_register_sample, uint, 0444);


static unsigned int sample_registers(void)
{
    return inb(0x1864) | ((unsigned int)inb(0x1874) << 8) |
           ((unsigned int)inb(0x1815) << 16) |
           ((unsigned int)inb(0x1821) << 24);
}

static void release_sci(struct timer_list *timer)
{
    unsigned int sample = sample_registers();
    bool invalid = (sample & 0xFFFF) == 0xFFFF ||
                   (sample >> 16) == 0xFFFF;

    if (!READ_ONCE(exiting) && invalid &&
        time_before(jiffies, release_deadline)) {
        mod_timer(&io_ready_timer, jiffies + msecs_to_jiffies(10));
        return;
    }
    if (invalid)
        quarantine_timeouts++;
    if (atomic_xchg(&sci_held, 0))
        enable_irq(sci_irq);
}

static u32 guard_event(acpi_handle device, u32 number, void *context)
{
    /* ACPICA has disabled the level event before calling this handler.
     * Do not evaluate AML or request re-enabling. No ACPI API may be called
     * here: the standard handler executes with the GPE spinlock held.
     */
    unsigned int sample = sample_registers();
    if (!suppressed_events)
        first_register_sample = sample;
    last_register_sample = sample;
    suppressed_events++;
    /* A standard GPE callback runs in interrupt context. Never sleep or
     * acquire ACPI locks here. Quarantine only the observed all-ones pair,
     * use a one-second release deadline and cap holds at four per load.
     * An inaccessible bank cannot produce useful status/enable information.
     */
    if (quarantine_bad_io && !READ_ONCE(exiting) && sample == ~0U &&
        quarantine_count < 4 && atomic_cmpxchg(&sci_held, 0, 1) == 0) {
        quarantine_count++;
        release_deadline = jiffies + msecs_to_jiffies(1000);
        disable_irq_nosync(sci_irq);
        mod_timer(&io_ready_timer, jiffies + msecs_to_jiffies(10));
    }
    pr_warn_ratelimited("surface_sl7_gpe_guard: contained firmware GPE 0xAB\n");
    return ACPI_INTERRUPT_HANDLED;
}

static int __init guard_init(void)
{
    acpi_handle method;
    acpi_status status;
    acpi_event_status flags;

    if (!enable_trial ||
        !dmi_match(DMI_SYS_VENDOR, "Microsoft Corporation") ||
        !dmi_match(DMI_PRODUCT_NAME,
                   "Surface Laptop for Business 7th Edition with Intel") ||
        !dmi_match(DMI_BIOS_VERSION, "18.106.143"))
        return -ENODEV;
    timer_setup(&io_ready_timer, release_sci, 0);
    if (quarantine_bad_io) {
        struct irq_data *data;
        if (acpi_gsi_to_irq(9, &sci_irq) || !sci_irq)
            return -ENODEV;
        data = irq_get_irq_data(sci_irq);
        /* enable_irq must be safe in timer/interrupt context. */
        if (!data || !data->chip || data->chip->irq_bus_lock ||
            data->chip->irq_bus_sync_unlock)
            return -EOPNOTSUPP;
    }
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_GPE._LAB", &method)))
        return -ENODEV;
    status = acpi_get_handle(NULL, "\\_GPE.SLAB", &method);
    if (status != AE_NOT_FOUND)
        return -ENODEV;
    status = acpi_get_gpe_status(NULL, BAD_GPE, &flags);
    if (ACPI_FAILURE(status) || !(flags & ACPI_EVENT_FLAG_MASKED) ||
        (flags & ACPI_EVENT_FLAG_ENABLE_SET))
        return -EBUSY;

    status = acpi_install_gpe_handler(NULL, BAD_GPE,
                                     ACPI_GPE_LEVEL_TRIGGERED,
                                     guard_event, NULL);
    if (ACPI_FAILURE(status))
        return -EIO;
    installed = true;
    status = acpi_get_gpe_status(NULL, BAD_GPE, &flags);
    if (ACPI_FAILURE(status) || (flags & ACPI_EVENT_FLAG_ENABLED)) {
        acpi_remove_gpe_handler(NULL, BAD_GPE, guard_event);
        installed = false;
        return -EBUSY;
    }
    /* Installing a handler drops the method's automatic runtime reference.
     * Unmask only after replacement, so ACPICA can physically disable the
     * event if firmware enables it again. Keeping mask_for_run set skips
     * the hardware write in acpi_hw_low_set_gpe(), including disable writes.
     */
    status = acpi_mask_gpe(NULL, BAD_GPE, FALSE);
    if (ACPI_FAILURE(status)) {
        acpi_remove_gpe_handler(NULL, BAD_GPE, guard_event);
        installed = false;
        return -EIO;
    }
    pr_info("surface_sl7_gpe_guard: experimental handler installed; AML unchanged\n");
    return 0;
}

static void __exit guard_exit(void)
{
    if (!installed)
        return;
    WRITE_ONCE(exiting, true);
    /* Restore the original masked policy before restoring the AML method. */
    acpi_mask_gpe(NULL, BAD_GPE, TRUE);
    timer_shutdown_sync(&io_ready_timer);
    if (atomic_xchg(&sci_held, 0))
        enable_irq(sci_irq);
    acpi_remove_gpe_handler(NULL, BAD_GPE, guard_event);
    pr_info("surface_sl7_gpe_guard: original masked method restored\n");
}
module_init(guard_init);
module_exit(guard_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental, firmware-specific Surface SL7 broken GPE guard");

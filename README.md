# Microsoft Surface Laptop 7 (Intel Lunar Lake) - Ubuntu / Linux Fixes

Collection of kernel drivers, systemd services, sleep preservation hooks, and desktop fixes for running Ubuntu / Linux on the **Microsoft Surface Laptop 7 (Intel Core Ultra Series 2 / Lunar Lake)**.

---

## Features & Fixes

### 1. Reliable Suspend / Sleep (`s2idle`) & GPE Quarantine
- **Problem:** ACPI GPE storms and SCI interrupt lockups prevent the Surface Laptop 7 from staying asleep, causing suspend to fail or loop indefinitely and drain the battery from 80% to 0% overnight.
- **Solution:** 
  - `surface_sl7_gpe_guard.ko`: Out-of-tree kernel driver that quarantines broken ACPI GPE interrupts and prevents SCI lockups.
  - `surface-wake-check`: Pre-suspend sanity checker that verifies hardware state, firmware compatibility, and driver integrity before every sleep transition.
  - `surface-wake-guard.service`: Systemd service ensuring the environment is safely primed on boot and before sleep.

### 2. Multi-Day Sleep Battery Preservation (3-Hour Auto-Poweroff)
- **Problem:** Even with proper `s2idle` sleep, Linux modern standby consumes ~1–1.3 W on battery. Leaving the laptop unplugged for multiple days will slowly drain the battery to empty.
- **Solution:**
  - `/lib/systemd/system-sleep/auto-poweroff-timer.sh`: Arms an RTC hardware wakealarm (`/sys/class/rtc/rtc0/wakealarm`) for 3 hours (10,800s) upon sleep when running on battery.
  - If the laptop wakes after 3 hours and the lid is still closed on battery, it safely initiates a clean system shutdown (`systemctl poweroff`).
  - If you open the lid or plug in AC power before the 3 hours expire, the timer automatically clears and resumes instantly.

### 3. GNOME Shell / Ubuntu Dock in Full-Screen Mode
- **Problem:** When hiding the top panel or running full-screen apps (e.g. Chrome, Codex, video players, games), moving the cursor to the bottom edge fails to reveal the dock, or the dock is rendered invisibly behind the app due to Wayland direct scanout.
- **Solution:**
  - `ubuntu-dock-fullscreen.patch`:
    1. Removes the legacy 2014 `user_time` suppression check in Ubuntu Dock that mistakenly treats cursor motion over full-screen apps as active typing/gaming.
    2. Broadens edge detection from a strict 1-pixel equality check (`y === height - 1`) to a 2-pixel boundary (`y >= height - 2`), making touchpad and touchscreen gestures reliably trigger the dock.
    3. Unconditionally disables direct scanout (`global.compositor.disable_unredirect()`) during dock slide-in so the Clutter desktop layer paints cleanly on top of full-screen Wayland surfaces.

### 4. 80% Smart Charging Hardware Limit
- **Note on Battery Behavior:** The Surface Laptop 7 features hardware-enforced **Smart Charging / Battery Limit** inside the Surface Embedded Controller (SAM EC) firmware. When plugged in, the device charges rapidly (~38 W) up to exactly **80%**, at which point the EC switches to `Not charging` to protect battery health and prevent swelling. This is managed by hardware firmware and is completely normal.

---

## Directory Structure

```text
surface-laptop-7-ubuntu-fixes/
├── README.md
├── install.sh
├── kernel-module/
│   ├── surface_sl7_gpe_guard.c
│   └── Makefile
├── systemd/
│   ├── surface-wake-guard.service
│   └── sleep-hooks/
│       └── auto-poweroff-timer.sh
├── scripts/
│   ├── surface-wake-check
│   ├── surface-wake-reboot
│   ├── surface-normal-reboot
│   ├── surface-audio-rollback
│   └── surface-touchpad-recover
└── gnome-shell/
    ├── ubuntu-dock-fullscreen.patch
    └── apply-dock-patch.sh
```

---

## Quick Installation

Clone this repository and run the installer:

```bash
git clone https://github.com/<your-username>/surface-laptop-7-linux-support.git
cd surface-laptop-7-linux-support
sudo ./install.sh
```

### Secure Boot Note:
If Secure Boot is enabled, sign the compiled module with your Machine Owner Key (MOK):
```bash
sudo kmodsign sha256 /path/to/MOK.priv /path/to/MOK.der /usr/local/lib/laptop-sleep-fix/surface_sl7_gpe_guard.ko
```
And verify the SHA-256 hash in `/usr/local/sbin/surface-wake-check`.

---

## License
GPL-2.0 / MIT

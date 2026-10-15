#!/bin/bash
# /lib/systemd/system-sleep/auto-poweroff-timer.sh
# Safely powers off the laptop if left unattended in suspend on battery for > 3 hours.

STATE_FILE="/run/auto-poweroff-timer.state"
SLEEP_TIMEOUT_SECS=10800  # 3 hours (10800 seconds)

case "$1" in
    pre)
        if [ "$2" = "suspend" ]; then
            charger_online=$(cat /sys/class/power_supply/ADP1/online 2>/dev/null || echo 0)
            if [ "$charger_online" -eq 0 ]; then
                now=$(date +%s)
                target=$((now + SLEEP_TIMEOUT_SECS))
                echo "$target" > "$STATE_FILE"
                echo 0 > /sys/class/rtc/rtc0/wakealarm 2>/dev/null
                echo "$target" > /sys/class/rtc/rtc0/wakealarm 2>/dev/null
                logger -t auto-poweroff "Pre-suspend: scheduled auto-poweroff wakealarm in $((SLEEP_TIMEOUT_SECS / 3600)) hours ($target) on battery."
            else
                rm -f "$STATE_FILE"
            fi
        fi
        ;;
    post)
        if [ "$2" = "suspend" ]; then
            echo 0 > /sys/class/rtc/rtc0/wakealarm 2>/dev/null
            if [ -f "$STATE_FILE" ]; then
                target=$(cat "$STATE_FILE" 2>/dev/null || echo 0)
                rm -f "$STATE_FILE"
                now=$(date +%s)
                charger_online=$(cat /sys/class/power_supply/ADP1/online 2>/dev/null || echo 0)
                lid_state=$(awk '{print $2}' /proc/acpi/button/lid/LID0/state 2>/dev/null || echo "open")

                if [ "$charger_online" -eq 0 ] && [ "$lid_state" = "closed" ] && [ "$now" -ge "$((target - 60))" ]; then
                    logger -t auto-poweroff "Unattended sleep timeout reached ($SLEEP_TIMEOUT_SECS s) with lid closed on battery. Initiating clean poweroff."
                    (sleep 3 && /usr/bin/systemctl poweroff) &
                else
                    logger -t auto-poweroff "System resumed: charger=$charger_online lid=$lid_state (now=$now target=$target). Continuing normal wake."
                fi
            fi
        fi
        ;;
esac

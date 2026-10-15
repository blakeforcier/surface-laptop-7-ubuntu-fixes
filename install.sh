#!/bin/bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ "$EUID" -ne 0 ]; then
    echo "This installation script must be run as root (sudo)." >&2
    exit 1
fi

echo "=== Microsoft Surface Laptop 7 (Intel Lunar Lake) Ubuntu Fixes ==="

# 1. Build and install kernel module
echo "--> Installing surface_sl7_gpe_guard kernel module..."
mkdir -p /usr/local/lib/laptop-sleep-fix
cd "$REPO_DIR/kernel-module"
make -C /lib/modules/$(uname -r)/build M="$REPO_DIR/kernel-module" modules
cp -f surface_sl7_gpe_guard.ko /usr/local/lib/laptop-sleep-fix/

# Check Secure Boot signing
if mokutil --sb-state 2>/dev/null | grep -qi "enabled"; then
    echo "Secure Boot is enabled. If you use a custom MOK, sign the module:"
    echo "  sudo kmodsign sha256 /path/to/MOK.priv /path/to/MOK.der /usr/local/lib/laptop-sleep-fix/surface_sl7_gpe_guard.ko"
    echo "And update the expected SHA256 in /usr/local/sbin/surface-wake-check."
fi

# 2. Install scripts
echo "--> Installing helper scripts to /usr/local/sbin/..."
cp -f "$REPO_DIR"/scripts/surface-* /usr/local/sbin/
chmod 755 /usr/local/sbin/surface-*

# 3. Install systemd service
echo "--> Installing surface-wake-guard.service..."
cp -f "$REPO_DIR/systemd/surface-wake-guard.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable surface-wake-guard.service

# 4. Install sleep hook
echo "--> Installing auto-poweroff-timer.sh sleep hook..."
mkdir -p /lib/systemd/system-sleep/
cp -f "$REPO_DIR/systemd/sleep-hooks/auto-poweroff-timer.sh" /lib/systemd/system-sleep/
chmod 755 /lib/systemd/system-sleep/auto-poweroff-timer.sh

# 5. Apply Ubuntu Dock patch
echo "--> Applying Ubuntu Dock full-screen autohide patch..."
bash "$REPO_DIR/gnome-shell/apply-dock-patch.sh"

echo "=== Installation complete! ==="

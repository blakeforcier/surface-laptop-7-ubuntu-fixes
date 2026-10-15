#!/bin/bash
set -euo pipefail

TARGET="/usr/share/gnome-shell/extensions/ubuntu-dock@ubuntu.com/docking.js"
PATCH_FILE="$(dirname "$0")/ubuntu-dock-fullscreen.patch"

if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)." >&2
    exit 1
fi

if [ ! -f "$TARGET" ]; then
    echo "Ubuntu Dock not found at $TARGET" >&2
    exit 1
fi

if [ ! -f "$TARGET.bak" ]; then
    echo "Creating backup at $TARGET.bak..."
    cp -a "$TARGET" "$TARGET.bak"
fi

echo "Applying fullscreen autohide and unredirection patch..."
patch -p1 -d / < "$PATCH_FILE" || true

echo "Patch applied. Restart GNOME Shell / reboot to take effect."

#!/bin/sh
# Grants the logged-in user access to Barco ClickShare USB/HID devices
# (vendor ID 0600), so that Wine can talk to the ClickShare Button.
#
# Usage: sudo sh scripts/install-udev-rule.sh
set -e

DIR=$(cd "$(dirname "$0")/.." && pwd)

if [ "$(id -u)" -ne 0 ]; then
    echo "Please run as root: sudo sh $0" >&2
    exit 1
fi

install -m 0644 "$DIR/udev/70-clickshare.rules" /etc/udev/rules.d/70-clickshare.rules
udevadm control --reload
udevadm trigger
echo "udev rule installed. Unplug and re-plug the ClickShare Button now."

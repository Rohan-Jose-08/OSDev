#!/bin/sh
set -e
. ./iso.sh

# QEMU 8.x has a known bug with mutex assertions
# Workaround: disable hardware acceleration completely
# Cursor is now visible for better synchronization
QEMU_OPTS="-no-shutdown -no-reboot -machine accel=tcg"

# Try GUI mode first, fall back to curses if no display available
if [ -n "$DISPLAY" ]; then
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -D ./qemu-debug-log -cdrom myos.iso $QEMU_OPTS
else
    echo "No DISPLAY set. Install an X server (VcXsrv/X410) or run with DISPLAY=:0"
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom myos.iso $QEMU_OPTS -display curses
fi libc
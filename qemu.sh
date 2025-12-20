#!/bin/sh
set -e
. ./iso.sh

# Try GUI mode first, fall back to curses if no display available
if [ -n "$DISPLAY" ]; then
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom myos.iso
else
    echo "No DISPLAY set. Install an X server (VcXsrv/X410) or run with DISPLAY=:0"
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom myos.iso -display curses
fi 
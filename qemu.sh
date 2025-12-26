#!/bin/sh
set -e
. ./iso.sh

# QEMU 8.x has a known bug with mutex assertions
# Workaround: disable hardware acceleration completely
# Cursor is now visible for better synchronization
QEMU_OPTS="-no-shutdown -no-reboot -machine accel=tcg -drive file=disk.img,format=raw,index=0,media=disk -d int,guest_errors,cpu_reset"

# Enable PC speaker audio when a PulseAudio server is available.
AUDIO_OPTS=""
if [ -S /mnt/wslg/PulseServer ]; then
    PULSE_SERVER="unix:/mnt/wslg/PulseServer"
    XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
    export PULSE_SERVER
    export XDG_RUNTIME_DIR
fi
if [ -n "$PULSE_SERVER" ]; then
    AUDIO_OPTS="-audiodev pa,id=pcspk -machine pcspk-audiodev=pcspk"
fi

# Try GUI mode first, fall back to curses if no display available
if [ -n "$DISPLAY" ]; then
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -D ./qemu-debug-log -cdrom myos.iso $QEMU_OPTS $AUDIO_OPTS
else
    echo "No DISPLAY set. Install an X server (VcXsrv/X410) or run with DISPLAY=:0"
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom myos.iso $QEMU_OPTS $AUDIO_OPTS -display curses
fi

#!/bin/sh
set -e
. ./iso.sh

# QEMU 8.x has a known bug with mutex assertions
# Workaround: disable hardware acceleration completely
# Cursor is now visible for better synchronization
NET_OPTS=${NET_OPTS:-"-netdev socket,id=net0,listen=:5555 -device rtl8139,netdev=net0"}
QEMU_OPTS="-no-shutdown -no-reboot -machine accel=tcg -drive file=disk.img,format=raw,index=0,media=disk $NET_OPTS -d int,guest_errors,cpu_reset"

# Enable AC97 audio. Choose with AUDIO_BACKEND=pa|sdl|wav|none.
AUDIO_OPTS=""
AUDIO_BACKEND=${AUDIO_BACKEND:-pa}
case "$AUDIO_BACKEND" in
    pa)
        if [ -S /mnt/wslg/PulseServer ]; then
            PULSE_SERVER="unix:/mnt/wslg/PulseServer"
            if [ -n "$AUDIO_PA_NATIVE" ] && [ -S /mnt/wslg/runtime-dir/pulse/native ]; then
                PULSE_SERVER="unix:/mnt/wslg/runtime-dir/pulse/native"
            fi
            XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
            PULSE_RUNTIME_PATH=/mnt/wslg/runtime-dir/pulse
            export PULSE_SERVER
            export XDG_RUNTIME_DIR
            export PULSE_RUNTIME_PATH
            if [ -f /mnt/wslg/runtime-dir/pulse/cookie ]; then
                PULSE_COOKIE=/mnt/wslg/runtime-dir/pulse/cookie
                export PULSE_COOKIE
            elif [ -f "$HOME/.config/pulse/cookie" ]; then
                PULSE_COOKIE="$HOME/.config/pulse/cookie"
                export PULSE_COOKIE
            elif [ -f "$HOME/.pulse-cookie" ]; then
                PULSE_COOKIE="$HOME/.pulse-cookie"
                export PULSE_COOKIE
            fi

            PA_OK=1
            if [ -z "$AUDIO_FORCE_PA" ] && [ -n "$AUDIO_CHECK_PA" ]; then
                if command -v pactl >/dev/null 2>&1; then
                    if command -v timeout >/dev/null 2>&1; then
                        timeout 1 pactl info >/dev/null 2>&1 || PA_OK=0
                    else
                        pactl info >/dev/null 2>&1 || PA_OK=0
                    fi
                else
                    PA_OK=0
                fi
            fi

            if [ "$PA_OK" -eq 1 ]; then
                AUDIO_OPTS="-audiodev pa,id=snd0,server=$PULSE_SERVER,out.fixed-settings=on,out.frequency=48000,out.channels=2,out.format=s16,in.fixed-settings=on,in.frequency=48000,in.channels=2,in.format=s16 -device AC97,audiodev=snd0"
            else
                echo "PulseAudio unavailable; running without audio (set AUDIO_FORCE_PA=1 to try anyway)."
            fi
        fi
        ;;
    sdl)
        AUDIO_OPTS="-audiodev sdl,id=snd0 -device AC97,audiodev=snd0"
        ;;
    wav)
        AUDIO_WAV_PATH=${AUDIO_WAV_PATH:-qemu-audio.wav}
        AUDIO_OPTS="-audiodev wav,id=snd0,path=$AUDIO_WAV_PATH -device AC97,audiodev=snd0"
        ;;
    none)
        AUDIO_OPTS=""
        ;;
    *)
        echo "Unknown AUDIO_BACKEND=$AUDIO_BACKEND; running without audio."
        ;;
esac

# Try GUI mode first, fall back to curses if no display available
if [ -n "$DISPLAY" ]; then
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -D ./qemu-debug-log -cdrom myos.iso $QEMU_OPTS $AUDIO_OPTS
else
    echo "No DISPLAY set. Install an X server (VcXsrv/X410) or run with DISPLAY=:0"
    qemu-system-$(./target-triplet-to-arch.sh $HOST) -cdrom myos.iso $QEMU_OPTS $AUDIO_OPTS -display curses
fi

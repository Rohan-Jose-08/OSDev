#!/bin/sh
set -e

TAP_DEV=${TAP_DEV:-tap0}
NET_OPTS_DEFAULT="-netdev tap,id=net0,ifname=${TAP_DEV},script=no,downscript=no -device rtl8139,netdev=net0"

./net-up.sh

if [ -z "$NET_OPTS" ]; then
  NET_OPTS="$NET_OPTS_DEFAULT" ./qemu.sh
else
  ./qemu.sh
fi

if [ -z "$KEEP_TAP" ]; then
  ./net-down.sh
else
  echo "KEEP_TAP set; leaving $TAP_DEV up."
fi

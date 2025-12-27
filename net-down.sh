#!/bin/sh
set -e

TAP_DEV=${TAP_DEV:-tap0}

if [ "$(id -u)" -ne 0 ]; then
  exec sudo TAP_DEV="$TAP_DEV" "$0" "$@"
fi

if ip link show "$TAP_DEV" >/dev/null 2>&1; then
  ip link del "$TAP_DEV"
  echo "Deleted $TAP_DEV."
else
  echo "$TAP_DEV not found."
fi

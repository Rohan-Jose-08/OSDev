#!/bin/sh
set -e

TAP_DEV=${TAP_DEV:-tap0}
TAP_IP=${TAP_IP:-10.0.2.2/24}
TAP_USER=${TAP_USER:-$USER}

if [ "$(id -u)" -ne 0 ]; then
  exec sudo TAP_DEV="$TAP_DEV" TAP_IP="$TAP_IP" TAP_USER="$TAP_USER" "$0" "$@"
fi

if ip link show "$TAP_DEV" >/dev/null 2>&1; then
  ip addr replace "$TAP_IP" dev "$TAP_DEV"
  ip link set "$TAP_DEV" up
  echo "${TAP_DEV} already exists; updated IP and brought up."
  exit 0
fi

ip tuntap add dev "$TAP_DEV" mode tap user "$TAP_USER"
ip addr add "$TAP_IP" dev "$TAP_DEV"
ip link set "$TAP_DEV" up

echo "Created $TAP_DEV with $TAP_IP (user=$TAP_USER)."

#!/bin/bash
# Launch one Linux application in its own KWin/Anland compositor session.
#
# Usage:
#   start-app-v5.sh <anland-socket> <command> [args...]
#
# This is intended for Droidspaces' WSLg-style app mode, where every Linux
# application gets a dedicated Anland broker socket and Android task/window.
set -euo pipefail

[ "$#" -ge 2 ] || {
    echo "usage: $0 <anland-socket> <command> [args...]" >&2
    exit 2
}

SOCK="$1"
shift
KWIN_BIN="${KWIN_BIN:-kwin_wayland}"

command -v "$KWIN_BIN" >/dev/null 2>&1 || {
    echo "kwin_wayland not found; install the Anland-patched KWin package" >&2
    exit 127
}

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
if [ ! -d "$XDG_RUNTIME_DIR" ]; then
    XDG_RUNTIME_DIR="$HOME/.local/run/anland-$(id -u)"
    mkdir -p "$XDG_RUNTIME_DIR"
fi
chmod 0700 "$XDG_RUNTIME_DIR"

unset DISPLAY
export ANLAND_SOCKET="$SOCK"
export ANLAND=1
export ANLAND_DRM_DEVICE="${ANLAND_DRM_DEVICE:-/dev/dri/renderD128}"
export MESA_LOADER_DRIVER_OVERRIDE="${MESA_LOADER_DRIVER_OVERRIDE:-kgsl}"
export GALLIUM_DRIVER="${GALLIUM_DRIVER:-kgsl}"
export FD_FORCE_KGSL="${FD_FORCE_KGSL:-1}"
export MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE="${MESA_VK_DEVICE_SELECT_FORCE_DEFAULT_DEVICE:-1}"
export XWAYLAND_GBM_DEVICE="${XWAYLAND_GBM_DEVICE:-$ANLAND_DRM_DEVICE}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
export GDK_BACKEND="${GDK_BACKEND:-wayland,x11}"
export MOZ_ENABLE_WAYLAND="${MOZ_ENABLE_WAYLAND:-1}"

exec dbus-run-session "$KWIN_BIN" --anland --xwayland --exit-with-session "$@"

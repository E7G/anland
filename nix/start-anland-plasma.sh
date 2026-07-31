#!/usr/bin/env bash
set -euo pipefail

SOCK=${ANLAND_SOCKET:-/run/display.sock}
RUNTIME=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}

if [ ! -S "$SOCK" ]; then
    echo "AnLand socket not found: $SOCK" >&2
    exit 1
fi

mkdir -p "$RUNTIME"
chmod 0700 "$RUNTIME"

pkill -9 plasmashell 2>/dev/null || true
pkill -9 kwin_wayland 2>/dev/null || true

export ANLAND_SOCKET="$SOCK"
export XDG_RUNTIME_DIR="$RUNTIME"
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland
export ANLAND_DRM_DEVICE=${ANLAND_DRM_DEVICE:-/dev/dri/renderD128}
unset DISPLAY

exec dbus-run-session startplasma-wayland

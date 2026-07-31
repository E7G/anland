#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
RESULT=${ANLAND_RESULT:-$(cat "$SCRIPT_DIR/result-path.txt")}
SOCK=${ANLAND_SOCKET:-/run/anland/display.sock}
RUNTIME=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}

if [ ! -S "$SOCK" ]; then
    echo "AnLand socket not found: $SOCK" >&2
    exit 1
fi

mkdir -p "$RUNTIME"
chmod 0700 "$RUNTIME"

rm -f "$RUNTIME"/wayland-*

export ANLAND_SOCKET="$SOCK"
export ANLAND=1
export XDG_RUNTIME_DIR="$RUNTIME"
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland
export MESA_LOADER_DRIVER_OVERRIDE=kgsl
export GALLIUM_DRIVER=kgsl
export FD_FORCE_KGSL=1
export LIBGL_DRIVERS_PATH="$RESULT/lib/dri"
export __EGL_VENDOR_LIBRARY_FILENAMES="$RESULT/share/glvnd/egl_vendor.d/50_mesa.json"
export QT_FORCE_STDERR_LOGGING=1
export QT_LOGGING_RULES="kwin_anland.debug=true;kwin_anland.info=true"
if [ -e /dev/dri/renderD128 ]; then
    export ANLAND_DRM_DEVICE=${ANLAND_DRM_DEVICE:-/dev/dri/renderD128}
fi
unset DISPLAY

if [[ ${1:-} != --session ]]; then
    exec dbus-run-session "$0" --session
fi

kwin_wayland --anland --no-lockscreen --no-global-shortcuts &
KWIN_PID=$!

for _ in {1..100}; do
    [[ -S "$RUNTIME/wayland-0" ]] && break
    kill -0 "$KWIN_PID" 2>/dev/null || break
    sleep 0.1
done

if [[ ! -S "$RUNTIME/wayland-0" ]]; then
    wait "$KWIN_PID"
    exit $?
fi

export WAYLAND_DISPLAY=wayland-0
"$RESULT/bin/plasmashell" &
PLASMA_PID=$!

set +e
wait "$KWIN_PID"
STATUS=$?
kill "$PLASMA_PID" 2>/dev/null
wait "$PLASMA_PID" 2>/dev/null
exit "$STATUS"

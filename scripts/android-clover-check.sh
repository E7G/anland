#!/system/bin/sh
set -eu

DS=/data/local/Droidspaces/bin/droidspaces
SOCK=/data/local/tmp/display_daemon.sock

command -v "$DS" >/dev/null
"$DS" check
test -S "$SOCK"
pgrep -f '/data/adb/modules/anland-daemon/display_daemon' >/dev/null
pgrep -f '/data/adb/modules/YH_YC/yhyc --yhyc' >/dev/null
echo 'Anland daemon, display socket and root-hide service are running.'


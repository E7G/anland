# Android WSLg-style profile (Xiaomi Pad 4 / clover)

This profile combines the Anland Android consumer/daemon with Droidspaces. The
root-hide module is installed separately by KernelSU (`YH_YC`); Anland's
`display_daemon` is installed by `anland-daemon`.

## Device requirements

* arm64 Android with root (KernelSU/Magisk/APatch)
* Droidspaces 6.5 or newer
* a rootfs containing `/sbin/init`
* Anland consumer APK and `display_daemon` module

## Bring-up

```sh
# replace SERIAL and ROOTFS for the target device
adb -s "$SERIAL" shell su -c 'droidspaces check'
adb -s "$SERIAL" shell su -c 'droidspaces --name=linux --rootfs=/data/local/Droidspaces/Containers/linux/rootfs --anland --gpu start'
adb -s "$SERIAL" shell su -c 'droidspaces --name=linux anland-session start 0'
adb -s "$SERIAL" shell su -c 'droidspaces --name=linux run /bin/sh -lc "echo $DISPLAY; uname -m"'
```

The Android app is `com.anland.consumer`. The daemon socket is
`/data/local/tmp/display_daemon.sock`; if a container configuration does not
use `--anland`, bind this socket into the rootfs with `-B` and export the
Anland variables required by the compositor.

## Known clover limitation

The stock 4.19 clover kernel used for testing has PID/UTS/IPC namespaces
disabled (`droidspaces check` reports these three required features missing).
Therefore Droidspaces cannot start an isolated container on that kernel. A
kernel rebuilt with `CONFIG_PID_NS`, `CONFIG_UTS_NS`, `CONFIG_IPC_NS` and
virtual DRM support is required for the full WSLg path. Until then use a
proot/rootless userspace (Termux + X11) profile; it does not provide kernel
namespace isolation.


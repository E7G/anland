#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

protocols=(
  common/protocol.h
  producers/kde/anland_backend_v5/src/backends/anland/protocol.h
  producers/kde/anland_backend_Arch_v5/src/backends/anland/protocol.h
  producers/kde/anland_backend_debian13_v5/src/backends/anland/protocol.h
  producers/kde/anland_backend_fedora43_v5/src/backends/anland/protocol.h
)

for p in "${protocols[@]:1}"; do
  cmp -s "${protocols[0]}" "$p" || {
    echo "protocol drift: $p differs from common/protocol.h" >&2
    exit 1
  }
done

cat >/tmp/anland-protocol-check.c <<'EOF'
#include "common/protocol.h"
_Static_assert(sizeof(struct InputEvent) == 20, "InputEvent ABI changed");
_Static_assert(sizeof(struct OutputEvent) == 20, "OutputEvent ABI changed");
_Static_assert(sizeof(struct window_event_payload_v1) == 28, "window payload v1 ABI changed");
int main(void) { return 0; }
EOF
cc -std=gnu11 -Wall -Wextra -Werror -I. /tmp/anland-protocol-check.c -o /tmp/anland-protocol-check
/tmp/anland-protocol-check

backends=(
  producers/kde/anland_backend_v5/src/backends/anland
  producers/kde/anland_backend_Arch_v5/src/backends/anland
  producers/kde/anland_backend_debian13_v5/src/backends/anland
  producers/kde/anland_backend_fedora43_v5/src/backends/anland
)
for d in "${backends[@]}"; do
  grep -q 'void setupWindowBridge();' "$d/anland_backend.h"
  grep -q 'void AnlandBackend::setupWindowBridge()' "$d/anland_backend.cpp"
  grep -q 'INPUT_TYPE_WINDOW_COMMAND' "$d/anland_backend.cpp"
  grep -q 'resendWindowSnapshot' "$d/anland_backend.cpp"
done

jni=consumers/anland_v5/android_consumer/app/src/main/jni/native_consumer.c
java=consumers/anland_v5/android_consumer/app/src/main/java/com/anland/consumer
grep -q 'nativeLinuxWindowEvent' "$jni"
grep -q 'nativeLinuxWindowEvent' "$java/MainActivity.java"
grep -q 'nativeSendWindowCommand' "$jni"
grep -q 'nativeSendWindowCommand' "$java/Native.java"

echo "WSLg window bridge protocol/source checks passed."

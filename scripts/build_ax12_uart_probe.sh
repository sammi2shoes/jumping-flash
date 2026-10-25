#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd "$(dirname "$0")/.." && pwd)
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r27c (27.2.12479018)}"
if [[ "$(cat "$ANDROID_NDK_HOME/source.properties")" != *"27.2.12479018"* ]]; then
  echo "Use pinned NDK r27c (27.2.12479018)." >&2
  exit 1
fi
ndk_compiler="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android28-clang"
"$ndk_compiler" -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror -Wl,-z,relro,-z,now -Wl,-z,max-page-size=16384 \
  "$project_root/android/ax12/app/src/main/cpp/uart_probe.c" \
  -o "$project_root/android/ax12/app/src/main/jniLibs/arm64-v8a/libjf_uart_probe.so"

python3 - "$project_root" <<'PY_HASH'
import hashlib, json, pathlib, sys
root = pathlib.Path(sys.argv[1]) / 'android/ax12/app/src/main'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
(root / 'cpp/uart_probe.build.json').write_text(json.dumps({
    'ndk_revision': '27.2.12479018', 'api_level': 28,
    'source_sha256': sha(root / 'cpp/uart_probe.c'),
    'binary_sha256': sha(root / 'jniLibs/arm64-v8a/libjf_uart_probe.so')
}, indent=2) + '\n')
PY_HASH

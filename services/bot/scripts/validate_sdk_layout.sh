#!/usr/bin/env bash
set -euo pipefail
SDK_DIR="${1:-/opt/sdk}"

required_paths=(
  "$SDK_DIR/include"
  "$SDK_DIR/lib"
  "$SDK_DIR/translation.json"
  "$SDK_DIR/zoomus.conf"
)

required_sos=(
  "libmeetingsdk.so"
  "libmeeting_service.so"
  "libzoom_rtc.so"
)

for p in "${required_paths[@]}"; do
  if [[ ! -e "$p" ]]; then
    echo "Missing SDK path: $p" >&2
    exit 1
  fi
done

for so in "${required_sos[@]}"; do
  if ! ls "$SDK_DIR/lib/$so" >/dev/null 2>&1; then
    echo "Missing SDK lib: $SDK_DIR/lib/$so" >&2
    exit 1
  fi
done

echo "SDK layout OK at $SDK_DIR"

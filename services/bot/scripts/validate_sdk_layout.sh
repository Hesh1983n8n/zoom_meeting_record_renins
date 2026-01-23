#!/usr/bin/env bash
set -euo pipefail
SDK_DIR="${1:-/opt/sdk}"
SDK="${SDK_DIR}"
echo "SDK root: ${SDK}"
ls -la "${SDK}"
ls -la "${SDK}/qt_libs" || true
find "${SDK}" -maxdepth 2 -name "*.so*" -print

required_paths=(
  "$SDK/h"
  "$SDK/json"
  "$SDK/qt_libs"
  "$SDK/libmeetingsdk.so"
)

missing=()
for p in "${required_paths[@]}"; do
  if [[ ! -e "$p" ]]; then
    missing+=("$p")
  fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
  echo "SDK layout check failed. Missing required paths:" >&2
  for p in "${missing[@]}"; do
    echo " - $p" >&2
  done
  exit 1
fi

echo "SDK layout OK at $SDK"

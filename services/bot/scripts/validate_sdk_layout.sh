#!/usr/bin/env bash
set -euo pipefail
SDK_DIR="${1:-/opt/sdk}"

required_paths=(
  "$SDK_DIR/h"
  "$SDK_DIR/json"
  "$SDK_DIR/qt_libs"
  "$SDK_DIR/libmeetingsdk.so"
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

if [[ ! -e "$SDK_DIR/libcmm.so" ]]; then
  echo "Warning: optional libcmm.so not found at $SDK_DIR/libcmm.so" >&2
fi

echo "SDK layout OK at $SDK_DIR"

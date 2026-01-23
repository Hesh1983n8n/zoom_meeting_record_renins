#!/usr/bin/env bash
set -euo pipefail

echo "[entrypoint] BOT_PORT=${BOT_PORT:-}"
echo "[entrypoint] LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}"

echo "[entrypoint] SDK contents:"
ls -la /opt/sdk || true

echo "[entrypoint] SDK qt_libs:"
ls -la /opt/sdk/qt_libs || true

if [[ -f /opt/sdk/libmeetingsdk.so ]]; then
  echo "[entrypoint] Found /opt/sdk/libmeetingsdk.so"
else
  echo "[entrypoint] Missing /opt/sdk/libmeetingsdk.so" >&2
fi

if [[ -d /opt/sdk/qt_libs ]]; then
  echo "[entrypoint] Found /opt/sdk/qt_libs"
else
  echo "[entrypoint] Missing /opt/sdk/qt_libs" >&2
fi

echo "[entrypoint] ldd /usr/local/bin/zoom_bot:"
ldd /usr/local/bin/zoom_bot || true

echo "[entrypoint] ldd /opt/sdk/libmeetingsdk.so:"
ldd /opt/sdk/libmeetingsdk.so || true

echo "[entrypoint] starting zoom_bot"
exec /usr/local/bin/zoom_bot

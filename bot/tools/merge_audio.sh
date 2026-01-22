#!/usr/bin/env bash
set -euo pipefail

MEETING_DIR="${1:?meeting dir required}"

shopt -s nullglob

for USER_DIR in "$MEETING_DIR"/users/*; do
  [ -d "$USER_DIR" ] || continue
  CHUNKS=("$USER_DIR"/chunks/*.wav)
  if [ ${#CHUNKS[@]} -eq 0 ]; then
    echo "No chunks for $USER_DIR, skip"
    continue
  fi

  LIST_FILE="$USER_DIR/files.txt"
  : > "$LIST_FILE"
  for f in "${CHUNKS[@]}"; do
    echo "file '$f'" >> "$LIST_FILE"
  done

  ffmpeg -hide_banner -loglevel error \
    -f concat -safe 0 -i "$LIST_FILE" \
    -ar 16000 -ac 1 "$USER_DIR/final.wav"

  echo "Merged: $USER_DIR/final.wav"
done

MIXED_DIR="$MEETING_DIR/mixed"
if [ -d "$MIXED_DIR/chunks" ]; then
  CHUNKS=("$MIXED_DIR"/chunks/*.wav)
  if [ ${#CHUNKS[@]} -gt 0 ]; then
    LIST_FILE="$MIXED_DIR/files.txt"
    : > "$LIST_FILE"
    for f in "${CHUNKS[@]}"; do
      echo "file '$f'" >> "$LIST_FILE"
    done

    ffmpeg -hide_banner -loglevel error \
      -f concat -safe 0 -i "$LIST_FILE" \
      -ar 16000 -ac 1 "$MIXED_DIR/final.wav"

    echo "Merged mixed audio: $MIXED_DIR/final.wav"
  fi
fi

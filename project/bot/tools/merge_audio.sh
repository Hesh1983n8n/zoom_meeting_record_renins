#!/usr/bin/env bash
set -euo pipefail

meeting_dir="${1:-}"
if [[ -z "${meeting_dir}" ]]; then
  echo "Usage: merge_audio.sh <meeting_dir>" >&2
  exit 1
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "ffmpeg is required to merge audio." >&2
  exit 1
fi

found_any=false

merge_user_dir() {
  local user_dir="$1"
  local chunks_dir="${user_dir}/chunks"
  local output_file="${user_dir}/final.wav"
  if [[ ! -d "${chunks_dir}" ]]; then
    return
  fi

  mapfile -t files < <(find "${chunks_dir}" -maxdepth 1 -type f -name "*.wav" | sort)
  if [[ "${#files[@]}" -eq 0 ]]; then
    return
  fi

  found_any=true
  local list_file
  list_file="${chunks_dir}/concat_list.txt"
  : > "${list_file}"
  for file in "${files[@]}"; do
    printf "file '%s'\n" "${file}" >> "${list_file}"
  done

  ffmpeg -y -f concat -safe 0 -i "${list_file}" -ar 16000 -ac 1 "${output_file}" >/dev/null 2>&1
}

if [[ -d "${meeting_dir}/users" ]]; then
  for user_dir in "${meeting_dir}"/users/*; do
    if [[ -d "${user_dir}" ]]; then
      merge_user_dir "${user_dir}"
    fi
  done
fi

if [[ -d "${meeting_dir}/mixed" ]]; then
  merge_user_dir "${meeting_dir}/mixed"
fi

if [[ "${found_any}" == "false" ]]; then
  echo "No valid WAV files found to merge. SDK integration is required to capture audio."
fi

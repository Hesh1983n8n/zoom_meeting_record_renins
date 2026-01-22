from __future__ import annotations

import shutil
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path


def _build_output_name(meeting_started_at: datetime) -> str:
    timestamp = meeting_started_at.astimezone(timezone.utc).strftime("%Y%m%d_%H%M%S")
    return f"{timestamp}.wav"


def merge_meeting_audio(
    meeting_path: Path,
    output_dir: Path,
    meeting_started_at: datetime,
) -> Path | None:
    wav_files = [
        wav_file
        for wav_file in sorted(meeting_path.glob("*.wav"))
        if wav_file.stat().st_size > 0
    ]
    if not wav_files:
        _cleanup_temp_meeting(meeting_path)
        return None

    output_dir.mkdir(parents=True, exist_ok=True)
    output_file = output_dir / _build_output_name(meeting_started_at)
    inputs = []
    for wav_file in wav_files:
        inputs.extend(["-i", str(wav_file)])

    command = [
        "ffmpeg",
        "-y",
        *inputs,
        "-filter_complex",
        f"amix=inputs={len(wav_files)}:duration=longest",
        str(output_file),
    ]
    subprocess.run(command, check=False)
    _cleanup_temp_meeting(meeting_path)
    return output_file


def _cleanup_temp_meeting(meeting_path: Path) -> None:
    if meeting_path.exists():
        shutil.rmtree(meeting_path, ignore_errors=True)


def cleanup_expired_records(output_dir: Path, retention_days: int) -> None:
    if retention_days <= 0 or not output_dir.exists():
        return
    cutoff = datetime.now(timezone.utc) - timedelta(days=retention_days)
    for wav_file in output_dir.glob("*.wav"):
        try:
            modified_time = datetime.fromtimestamp(wav_file.stat().st_mtime, tz=timezone.utc)
        except OSError:
            continue
        if modified_time < cutoff:
            wav_file.unlink(missing_ok=True)

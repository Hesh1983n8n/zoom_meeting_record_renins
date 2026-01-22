from __future__ import annotations

import subprocess
from pathlib import Path


def merge_meeting_audio(meeting_path: Path) -> Path | None:
    wav_files = sorted(meeting_path.glob("*.wav"))
    if not wav_files:
        return None

    output_file = meeting_path / "mix.wav"
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
    return output_file

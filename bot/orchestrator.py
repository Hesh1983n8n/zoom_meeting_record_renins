from __future__ import annotations

import json
import logging
import os
import subprocess
from datetime import datetime, timedelta, timezone
from pathlib import Path


logger = logging.getLogger("zoom-bot")


def run_zoom_recording(
    meeting_id: str,
    passcode: str | None,
    display_name: str,
) -> None:
    data_dir = Path(os.environ.get("DATA_DIR", "/data"))
    retention_days = int(os.environ.get("RECORD_RETENTION_DAYS", "5"))
    recorder_bin = os.environ.get("ZOOM_RECORDER_BIN", "/app/bin/zoom_bot_recorder")
    merge_bin = os.environ.get("MERGE_BIN", "/app/bin/merge_audio")

    meeting_dir = data_dir / "meetings" / meeting_id
    meeting_dir.mkdir(parents=True, exist_ok=True)
    metadata_path = meeting_dir / "metadata.json"
    metadata = {
        "meeting_id": meeting_id,
        "display_name": display_name,
        "passcode_provided": bool(passcode),
        "created_at": datetime.now(timezone.utc).isoformat(),
    }
    metadata_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")

    logger.info("START join_meeting (SDK)")
    cmd = [
        recorder_bin,
        "--meeting_id",
        meeting_id,
        "--display_name",
        display_name,
        "--out_dir",
        str(meeting_dir),
    ]
    if passcode:
        cmd += ["--passcode", passcode]

    if not Path(recorder_bin).exists():
        logger.error("FAIL join_meeting: recorder binary not found at %s", recorder_bin)
        return

    logger.info("START capture_audio (SDK callbacks)")
    logger.info("Running recorder: %s", " ".join(cmd))
    result = subprocess.run(cmd, check=False)
    if result.returncode != 0:
        logger.error("FAIL capture_audio: recorder exit code %s", result.returncode)
        return
    logger.info("OK capture_audio (SDK callbacks)")
    logger.info("OK join_meeting (SDK)")

    logger.info("START merge_audio")
    if not Path(merge_bin).exists():
        logger.error("FAIL merge_audio: merge binary not found at %s", merge_bin)
    else:
        _log_meeting_contents(meeting_dir)
        subprocess.run([merge_bin, str(meeting_dir)], check=False)
        logger.info("OK merge_audio")

    _cleanup_old_meetings(data_dir / "meetings", retention_days)


def _log_meeting_contents(meeting_dir: Path) -> None:
    logger.info("meeting_dir=%s", meeting_dir)
    files = sorted(meeting_dir.rglob("*"))
    for path in files[:200]:
        logger.info("file=%s", path)
    wav_count = len(list(meeting_dir.rglob("*.wav")))
    logger.info("wav_count=%s", wav_count)


def _cleanup_old_meetings(root: Path, retention_days: int) -> None:
    if retention_days <= 0 or not root.exists():
        return
    cutoff = datetime.now(timezone.utc) - timedelta(days=retention_days)
    for meeting_dir in root.iterdir():
        if not meeting_dir.is_dir():
            continue
        try:
            modified_time = datetime.fromtimestamp(meeting_dir.stat().st_mtime, tz=timezone.utc)
        except OSError:
            continue
        if modified_time < cutoff:
            logger.info("Removing expired meeting dir: %s", meeting_dir)
            for child in meeting_dir.rglob("*"):
                if child.is_file():
                    child.unlink(missing_ok=True)
            for child in sorted(meeting_dir.rglob("*"), reverse=True):
                if child.is_dir():
                    child.rmdir()
            meeting_dir.rmdir()

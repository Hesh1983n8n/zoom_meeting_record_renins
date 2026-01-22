from __future__ import annotations

import os
from datetime import datetime, timezone
from pathlib import Path

import logging

from fastapi import FastAPI
from pydantic import BaseModel, Field

from merge_audio import cleanup_expired_records, merge_meeting_audio
from sdk_placeholder import capture_audio_placeholder, start_zoom_meeting_record


class JoinRequest(BaseModel):
    meeting_url: str = Field(..., min_length=10)
    display_name: str = Field(default="Meet.Ai", min_length=1)
    passcode: str | None = None


class JoinResponse(BaseModel):
    status: str
    meeting_id: str


app = FastAPI(title="Zoom Meeting Bot")
logger = logging.getLogger("zoom-bot")


@app.post("/join", response_model=JoinResponse)
def join_meeting(payload: JoinRequest) -> JoinResponse:
    logger.info("START join_meeting")
    record_dir = Path(os.environ.get("RECORD_DIR", "/data/records"))
    temp_record_dir = Path(os.environ.get("TEMP_RECORD_DIR", "/data/records_temp"))
    retention_days = int(os.environ.get("RECORD_RETENTION_DAYS", "5"))
    meeting_started_at = datetime.now(timezone.utc)
    meeting_id, passcode = start_zoom_meeting_record(
        meeting_url=payload.meeting_url,
        display_name=payload.display_name,
        passcode=payload.passcode,
        record_dir=temp_record_dir,
    )
    logger.info("OK join_meeting (meeting_id=%s).", meeting_id)
    meeting_path = temp_record_dir / meeting_id
    logger.info("START capture_audio")
    capture_audio_placeholder(meeting_id, temp_record_dir)
    logger.info("START merge_audio")
    merged_file = merge_meeting_audio(meeting_path, record_dir, meeting_started_at)
    if merged_file is None:
        logger.warning("FAIL merge_audio: no valid WAV files found.")
    else:
        logger.info("OK merge_audio: %s", merged_file)
    cleanup_expired_records(record_dir, retention_days)
    logger.info("OK cleanup_expired_records")
    logger.info("DONE join_meeting")
    return JoinResponse(status="queued", meeting_id=meeting_id)

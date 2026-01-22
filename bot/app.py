from __future__ import annotations

import os
from datetime import datetime, timezone
from pathlib import Path

from fastapi import FastAPI
from pydantic import BaseModel, Field

from merge_audio import cleanup_expired_records, merge_meeting_audio
from sdk_placeholder import start_zoom_meeting_record


class JoinRequest(BaseModel):
    meeting_url: str = Field(..., min_length=10)
    display_name: str = Field(default="Meet.Ai", min_length=1)


class JoinResponse(BaseModel):
    status: str
    meeting_id: str


app = FastAPI(title="Zoom Meeting Bot")


@app.post("/join", response_model=JoinResponse)
def join_meeting(payload: JoinRequest) -> JoinResponse:
    record_dir = Path(os.environ.get("RECORD_DIR", "/data/records"))
    temp_record_dir = Path(os.environ.get("TEMP_RECORD_DIR", "/data/records_temp"))
    retention_days = int(os.environ.get("RECORD_RETENTION_DAYS", "5"))
    meeting_started_at = datetime.now(timezone.utc)
    meeting_id, passcode = start_zoom_meeting_record(
        meeting_url=payload.meeting_url,
        display_name=payload.display_name,
        record_dir=temp_record_dir,
    )
    meeting_path = temp_record_dir / meeting_id
    merge_meeting_audio(meeting_path, record_dir, meeting_started_at)
    cleanup_expired_records(record_dir, retention_days)
    return JoinResponse(status="queued", meeting_id=meeting_id)

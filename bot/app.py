from __future__ import annotations

import logging

from fastapi import FastAPI
from pydantic import BaseModel, Field

from orchestrator import run_zoom_recording
from parsing import parse_meeting_details


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
    meeting_id, passcode = parse_meeting_details(
        payload.meeting_url,
        payload.passcode,
    )
    logger.info("OK join_meeting (meeting_id=%s).", meeting_id)
    run_zoom_recording(
        meeting_id=meeting_id,
        passcode=passcode,
        display_name=payload.display_name,
    )
    logger.info("DONE join_meeting")
    return JoinResponse(status="queued", meeting_id=meeting_id)

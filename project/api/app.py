import json
import os
import re
import time
from typing import Optional
from urllib.parse import parse_qs, urlparse

import jwt
import redis
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel


class JoinRequest(BaseModel):
    meeting_url: str
    passcode: Optional[str] = None


app = FastAPI()

REDIS_URL = os.getenv("REDIS_URL", "redis://redis:6379/0")
QUEUE_NAME = os.getenv("QUEUE_NAME", "zoom_jobs")
BOT_DISPLAY_NAME = os.getenv("BOT_DISPLAY_NAME", "Meet.Ai")
SDK_KEY = os.getenv("ZOOM_MEETING_SDK_KEY", "")
SDK_SECRET = os.getenv("ZOOM_MEETING_SDK_SECRET", "")

redis_client = redis.Redis.from_url(REDIS_URL, decode_responses=True)


def parse_meeting_id(meeting_url: str) -> str:
    parsed = urlparse(meeting_url)
    match = re.search(r"/j/(\d+)", parsed.path)
    if match:
        return match.group(1)
    fallback = re.search(r"(\d{9,})", parsed.path)
    if fallback:
        return fallback.group(1)
    raise ValueError("Unable to parse meeting_id from URL")


def parse_passcode(meeting_url: str, explicit_passcode: Optional[str]) -> Optional[str]:
    if explicit_passcode:
        return explicit_passcode
    parsed = urlparse(meeting_url)
    query = parse_qs(parsed.query)
    return query.get("pwd", [None])[0]


def generate_signature(meeting_id: str) -> str:
    if not SDK_KEY or not SDK_SECRET:
        raise ValueError("Missing ZOOM_MEETING_SDK_KEY or ZOOM_MEETING_SDK_SECRET")
    timestamp = int(time.time())
    exp = timestamp + 300
    payload = {
        "sdkKey": SDK_KEY,
        "mn": meeting_id,
        "role": 0,
        "iat": timestamp,
        "exp": exp,
        "appKey": SDK_KEY,
        "tokenExp": exp,
    }
    return jwt.encode(payload, SDK_SECRET, algorithm="HS256")


@app.post("/join")
async def join_meeting(payload: JoinRequest):
    try:
        meeting_id = parse_meeting_id(payload.meeting_url)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    passcode = parse_passcode(payload.meeting_url, payload.passcode)
    try:
        signature = generate_signature(meeting_id)
    except ValueError as exc:
        raise HTTPException(status_code=500, detail=str(exc)) from exc

    job = {
        "meeting_id": meeting_id,
        "passcode": passcode,
        "meeting_url": payload.meeting_url,
        "display_name": BOT_DISPLAY_NAME,
        "signature": signature,
    }

    redis_client.lpush(QUEUE_NAME, json.dumps(job))

    return {"ok": True, "enqueued": True, "meeting_id": meeting_id}

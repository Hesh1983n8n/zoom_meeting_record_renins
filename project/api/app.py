import base64
import json
import logging
import os
import re
import time
from typing import Optional
from urllib.parse import parse_qs, urlparse

import jwt
import redis
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel


class JoinRequest(BaseModel):
    meeting_url: str
    passcode: Optional[str] = None


app = FastAPI()

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("zoom-bot-api")


@app.get("/")
async def root():
    return {
        "ok": True,
        "service": "Zoom Bot API",
        "endpoints": {"POST /join": "enqueue zoom meeting join job"},
        "docs": "/docs",
        "ui": "/ui",
    }


@app.get("/health")
async def health():
    return {"ok": True}


@app.get("/ui", response_class=HTMLResponse)
def ui():
    return """
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <title>Zoom Bot API</title>
    <style>
      body { font-family: Arial, sans-serif; margin: 40px; max-width: 720px; }
      label { display: block; margin-top: 16px; font-weight: 600; }
      input { width: 100%; padding: 8px; margin-top: 6px; }
      button { margin-top: 16px; padding: 10px 16px; }
      pre { background: #f4f4f4; padding: 12px; white-space: pre-wrap; }
    </style>
  </head>
  <body>
    <h1>Zoom Bot API</h1>
    <p>Подключение Meet.Ai к Zoom встрече.</p>
    <p><a href="/docs">Документация /docs</a> • <a href="/health">Health</a></p>
    <label for="meeting_url">Meeting URL (обязательный)</label>
    <input id="meeting_url" type="text" placeholder="https://zoom.us/j/123456789?pwd=abc" />
    <label for="passcode">Passcode (опционально)</label>
    <input id="passcode" type="text" placeholder="optional" />
    <button id="submit">Подключить Meet.Ai</button>
    <h2>Результат</h2>
    <pre id="result">Ожидание запроса...</pre>
    <script>
      const resultEl = document.getElementById("result");
      document.getElementById("submit").addEventListener("click", async () => {
        const meetingUrl = document.getElementById("meeting_url").value.trim();
        const passcode = document.getElementById("passcode").value.trim();
        if (!meetingUrl) {
          resultEl.textContent = "Ошибка: meeting_url обязателен.";
          return;
        }
        resultEl.textContent = "Отправка запроса...";
        try {
          const payload = { meeting_url: meetingUrl };
          if (passcode) {
            payload.passcode = passcode;
          }
          const response = await fetch("/join", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(payload),
          });
          const data = await response.json();
          if (!response.ok) {
            resultEl.textContent = `Ошибка ${response.status}: ${JSON.stringify(data)}`;
            return;
          }
          const output = {
            job_id: data.job_id ?? null,
            meeting_id: data.meeting_id ?? null,
            ok: data.ok,
            enqueued: data.enqueued,
          };
          resultEl.textContent = JSON.stringify(output, null, 2);
        } catch (error) {
          resultEl.textContent = `Ошибка запроса: ${error}`;
        }
      });
    </script>
  </body>
</html>
"""


@app.get("/ui/health")
async def ui_health():
    return {"ok": True}

REDIS_URL = os.getenv("REDIS_URL", "redis://redis:6379/0")
QUEUE_NAME = os.getenv("QUEUE_NAME", "zoom_jobs")
BOT_DISPLAY_NAME = os.getenv("BOT_DISPLAY_NAME", "Meet.Ai")
SDK_KEY = os.getenv("ZOOM_MEETING_SDK_KEY", "")
SDK_SECRET = os.getenv("ZOOM_MEETING_SDK_SECRET", "")
SIGNATURE_MODE = os.getenv("SIGNATURE_MODE", "sdk_auth").lower()

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


def _build_sdk_auth_payload(timestamp: int, exp: int) -> dict:
    return {
        "sdkKey": SDK_KEY,
        "iat": timestamp,
        "exp": exp,
        "tokenExp": exp,
    }


def _build_meeting_sdk_payload(meeting_id: str, timestamp: int, exp: int) -> dict:
    return {
        "sdkKey": SDK_KEY,
        "mn": meeting_id,
        "role": 0,
        "iat": timestamp,
        "exp": exp,
        "appKey": SDK_KEY,
        "tokenExp": exp,
    }


def _decode_jwt_payload(token: str) -> dict:
    try:
        payload_b64 = token.split(".")[1]
        padded = payload_b64 + "=" * (-len(payload_b64) % 4)
        decoded = base64.urlsafe_b64decode(padded.encode("utf-8"))
        return json.loads(decoded.decode("utf-8"))
    except (IndexError, ValueError, json.JSONDecodeError) as exc:
        raise ValueError("Unable to decode JWT payload") from exc


def _log_payload_times(payload: dict) -> None:
    now = int(time.time())
    iat = payload.get("iat")
    exp = payload.get("exp")
    token_exp = payload.get("tokenExp")
    logger.info(
        "jwt_payload_times iat=%s exp=%s tokenExp=%s now=%s",
        iat,
        exp,
        token_exp,
        now,
    )
    if iat is None or exp is None or token_exp is None:
        logger.error("auth rc=15 name=SDKERR_UNAUTHENTICATION reason=missing_fields")
        raise ValueError("JWT payload missing required time fields")
    if iat > now or exp <= now or token_exp <= now:
        logger.error("auth rc=15 name=SDKERR_UNAUTHENTICATION reason=time_invalid")
        raise ValueError("JWT time validation failed")


def generate_signature(meeting_id: str) -> str:
    if not SDK_KEY or not SDK_SECRET:
        raise ValueError("Missing ZOOM_MEETING_SDK_KEY or ZOOM_MEETING_SDK_SECRET")
    timestamp = int(time.time())
    exp = timestamp + 300
    if SIGNATURE_MODE == "sdk_auth":
        payload = _build_sdk_auth_payload(timestamp, exp)
    elif SIGNATURE_MODE == "meeting_sdk":
        payload = _build_meeting_sdk_payload(meeting_id, timestamp, exp)
    else:
        raise ValueError(f"Unsupported SIGNATURE_MODE: {SIGNATURE_MODE}")

    token = jwt.encode(payload, SDK_SECRET, algorithm="HS256")
    decoded_payload = _decode_jwt_payload(token)
    logger.info("jwt_mode=%s payload=%s", SIGNATURE_MODE, decoded_payload)
    _log_payload_times(decoded_payload)
    return token


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

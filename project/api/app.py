import base64
import json
import logging
import os
import time
from typing import Optional

import jwt
import redis
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

from zoom_url import parse_zoom_meeting_url


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
    <input id="meeting_url" type="text" placeholder="https://zoom.us/j/123456789" />
    <label for="passcode">Passcode (опционально)</label>
    <input id="passcode" type="text" placeholder="Например: 123456" />
    <p>
      Passcode — это код доступа (обычно 6–10 символов), а не значение
      <code>pwd=</code> из ссылки. Если не указать passcode, вход может не пройти.
    </p>
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
redis_client = redis.Redis.from_url(REDIS_URL, decode_responses=True)


def parse_passcode(explicit_passcode: Optional[str]) -> Optional[str]:
    """
    IMPORTANT:
    Zoom URL query param `pwd=` is NOT the meeting passcode.
    Meeting SDK JoinParam.psw expects the human-readable passcode (usually 6-10 chars),
    which must be supplied explicitly by the user.
    """
    if not explicit_passcode:
        return None
    trimmed = explicit_passcode.strip()
    if not trimmed:
        return None
    if "." in trimmed or len(trimmed) > 16:
        logger.warning("passcode_rejected value=%s", trimmed)
        return None
    return trimmed


def _build_sdk_auth_payload(now: int) -> dict:
    return {
        "appKey": SDK_KEY,
        "iat": now - 30,
        "exp": now + 60 * 60,
        "tokenExp": now + 60 * 60,
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
    if exp <= now or token_exp <= now:
        logger.error("auth rc=15 name=SDKERR_UNAUTHENTICATION reason=time_invalid")
        raise ValueError("JWT time validation failed")


def generate_signature(meeting_id: str) -> str:
    if not SDK_KEY or not SDK_SECRET:
        raise ValueError("Missing ZOOM_MEETING_SDK_KEY or ZOOM_MEETING_SDK_SECRET")
    now = int(time.time())
    payload = _build_sdk_auth_payload(now)

    token = jwt.encode(payload, SDK_SECRET, algorithm="HS256")
    if isinstance(token, bytes):
        token = token.decode("utf-8")
    decoded_payload = _decode_jwt_payload(token)
    logger.info("jwt_mode=sdk_auth payload=%s", decoded_payload)
    _log_payload_times(decoded_payload)
    return token


@app.post("/join")
async def join_meeting(payload: JoinRequest):
    try:
        parsed = parse_zoom_meeting_url(payload.meeting_url)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    meeting_id = parsed["meeting_id"]
    pwd_token = parsed["pwd_token"]

    passcode = parse_passcode(payload.passcode)
    try:
        signature = generate_signature(meeting_id)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    decoded_payload = _decode_jwt_payload(signature)
    now = int(time.time())
    iat = decoded_payload.get("iat")
    exp = decoded_payload.get("exp")
    delta_exp = exp - now if isinstance(exp, int) else None
    print("JWT_DEBUG now=", now, "iat=", iat, "exp=", exp, "delta_exp=", delta_exp)
    if delta_exp is None or delta_exp <= 0:
        raise HTTPException(status_code=400, detail="JWT exp is not valid")

    job = {
        "meeting_id": meeting_id,
        "passcode": passcode,
        "pwd_token": pwd_token,
        "meeting_url": payload.meeting_url,
        "display_name": BOT_DISPLAY_NAME,
        "signature": signature,
    }

    redis_client.lpush(QUEUE_NAME, json.dumps(job))

    return {"ok": True, "enqueued": True, "meeting_id": meeting_id}

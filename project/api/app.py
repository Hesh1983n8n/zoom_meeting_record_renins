import json
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

import base64
import hashlib
import hmac
import json
import logging
import os
import secrets
import time
from typing import Optional

import redis
import requests
from fastapi import FastAPI, HTTPException
from fastapi.responses import HTMLResponse, RedirectResponse
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
    return RedirectResponse(url="/ui")


@app.get("/health")
async def health():
    return {"ok": True}


@app.get("/ui", response_class=HTMLResponse)
def ui():
    return HTMLResponse(
        content="""
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
    <p><a href="/oauth/start">Подключить Zoom OAuth</a></p>
    <label for="meeting_url">Meeting URL (обязательный)</label>
    <input id="meeting_url" type="text" placeholder="https://zoom.us/j/123456789" />
    <label for="passcode">Passcode (опционально)</label>
    <input id="passcode" type="text" placeholder="Например: 123456" />
    <p>
      Passcode — это код доступа (обычно 6–10 символов), а не значение
      <code>pwd=</code> из ссылки. Если не указать passcode, вход может не пройти.
    </p>
    <p>
      Чеклист: если после входа видите <code>status=ENDED result=63</code>,
      проверьте auth-only режим, waiting room, требования регистрации и E2EE.
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
""",
        status_code=200,
    )


@app.get("/ui/health")
async def ui_health():
    return {"ok": True}

REDIS_URL = os.getenv("REDIS_URL", "redis://redis:6379/0")
QUEUE_NAME = os.getenv("QUEUE_NAME", "zoom_jobs")
BOT_DISPLAY_NAME = os.getenv("BOT_DISPLAY_NAME", "Meet.Ai")
MEETING_SDK_KEY = os.getenv("ZOOM_MEETING_SDK_KEY", "")
MEETING_SDK_SECRET = os.getenv("ZOOM_MEETING_SDK_SECRET", "")
OAUTH_CLIENT_ID = os.getenv("ZOOM_OAUTH_CLIENT_ID", "")
OAUTH_CLIENT_SECRET = os.getenv("ZOOM_OAUTH_CLIENT_SECRET", "")
PUBLIC_BASE_URL = os.getenv("PUBLIC_BASE_URL", "")
OAUTH_REDIRECT_PATH = os.getenv("OAUTH_REDIRECT_PATH", "/oauth/callback")
OAUTH_BASE_URL = os.getenv("OAUTH_BASE_URL", "")
MEETAI_API_KEY = os.getenv("MEETAI_API_KEY", "")
redis_client = redis.Redis.from_url(REDIS_URL, decode_responses=True)


def parse_passcode(meeting_url: str, explicit_passcode: Optional[str]) -> Optional[str]:
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
    return trimmed


def _oauth_redirect_uri() -> str:
    if not PUBLIC_BASE_URL:
        raise HTTPException(status_code=500, detail="PUBLIC_BASE_URL is not configured")
    base = PUBLIC_BASE_URL.rstrip("/")
    path = OAUTH_REDIRECT_PATH if OAUTH_REDIRECT_PATH.startswith("/") else f"/{OAUTH_REDIRECT_PATH}"
    return f"{base}{path}"


@app.get("/oauth/start")
async def oauth_start():
    if not OAUTH_CLIENT_ID or not OAUTH_CLIENT_SECRET:
        raise HTTPException(status_code=500, detail="Zoom OAuth client is not configured")
    redirect_uri = _oauth_redirect_uri()
    state = secrets.token_urlsafe(16)
    redis_client.setex(f"zoom_oauth_state:{state}", 600, "1")
    auth_url = (
        "https://zoom.us/oauth/authorize"
        f"?response_type=code&client_id={OAUTH_CLIENT_ID}"
        f"&redirect_uri={redirect_uri}&state={state}"
    )
    return RedirectResponse(url=auth_url)


@app.get("/oauth/callback")
async def oauth_callback(code: Optional[str] = None, state: Optional[str] = None):
    if not code:
        raise HTTPException(status_code=400, detail="Missing code")
    if not state:
        raise HTTPException(status_code=400, detail="Missing state")
    state_key = f"zoom_oauth_state:{state}"
    if not redis_client.get(state_key):
        raise HTTPException(status_code=400, detail="Invalid state")
    redis_client.delete(state_key)

    redirect_uri = _oauth_redirect_uri()
    auth_header = base64.b64encode(f"{OAUTH_CLIENT_ID}:{OAUTH_CLIENT_SECRET}".encode("utf-8")).decode(
        "utf-8"
    )
    response = requests.post(
        "https://zoom.us/oauth/token",
        params={
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": redirect_uri,
        },
        headers={
            "Authorization": f"Basic {auth_header}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
        timeout=20,
    )
    if response.status_code != 200:
        logger.error("oauth_token_error status=%s body=%s", response.status_code, response.text)
        raise HTTPException(status_code=502, detail="Zoom OAuth token exchange failed")

    token_payload = response.json()
    token_payload["obtained_at"] = int(time.time())
    redis_client.set("zoom_oauth_tokens", json.dumps(token_payload))
    return {"ok": True, "stored": True, "token_type": token_payload.get("token_type")}


def _decode_jwt_payload(token: str) -> dict:
    try:
        payload_b64 = token.split(".")[1]
        padded = payload_b64 + "=" * (-len(payload_b64) % 4)
        decoded = base64.urlsafe_b64decode(padded.encode("utf-8"))
        return json.loads(decoded.decode("utf-8"))
    except (IndexError, ValueError, json.JSONDecodeError) as exc:
        raise ValueError("Unable to decode JWT payload") from exc


def _base64url_encode(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).decode("utf-8").rstrip("=")


def build_meeting_sdk_signature() -> str:
    if not MEETING_SDK_KEY or not MEETING_SDK_SECRET:
        raise ValueError("Missing ZOOM_MEETING_SDK_KEY or ZOOM_MEETING_SDK_SECRET")
    now = int(time.time())
    payload = {
        "appKey": MEETING_SDK_KEY,
        "iat": now - 30,
        "exp": now + 60 * 60,
        "tokenExp": now + 60 * 60,
    }
    header = {"alg": "HS256", "typ": "JWT"}
    header_b64 = _base64url_encode(json.dumps(header, separators=(",", ":")).encode("utf-8"))
    payload_b64 = _base64url_encode(json.dumps(payload, separators=(",", ":")).encode("utf-8"))
    signing_input = f"{header_b64}.{payload_b64}".encode("utf-8")
    signature = hmac.new(
        MEETING_SDK_SECRET.encode("utf-8"),
        signing_input,
        hashlib.sha256,
    ).digest()
    token = f"{header_b64}.{payload_b64}.{_base64url_encode(signature)}"
    logger.info("jwt_mode=local payload=%s", payload)
    return token


def fetch_meeting_sdk_signature() -> str:
    if MEETING_SDK_KEY and MEETING_SDK_SECRET:
        return build_meeting_sdk_signature()
    if not OAUTH_BASE_URL or not MEETAI_API_KEY:
        raise ValueError("Missing OAUTH_BASE_URL or MEETAI_API_KEY")
    url = f"{OAUTH_BASE_URL.rstrip('/')}/token/meeting-sdk-jwt"
    response = requests.get(url, headers={"X-API-Key": MEETAI_API_KEY}, timeout=20)
    if response.status_code != 200:
        logger.error(
            "oauth_signature_error status=%s body=%s",
            response.status_code,
            response.text,
        )
        raise ValueError("Failed to fetch meeting SDK signature")
    payload = response.json()
    signature = payload.get("signature")
    if not signature:
        raise ValueError("Signature missing in OAuth response")
    decoded_payload = _decode_jwt_payload(signature)
    if decoded_payload:
        logger.info("jwt_mode=remote payload=%s", decoded_payload)
    return signature


@app.post("/join")
async def join_meeting(payload: JoinRequest):
    try:
        parsed = parse_zoom_meeting_url(payload.meeting_url)
    except ValueError as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc

    meeting_id = parsed["meeting_id"]
    pwd_token = parsed["pwd_token"]

    passcode = parse_passcode(payload.meeting_url, payload.passcode)
    try:
        signature = fetch_meeting_sdk_signature()
    except ValueError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc

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
        "meeting_url_raw": payload.meeting_url,
        "display_name": BOT_DISPLAY_NAME,
        "signature": signature,
    }

    redis_client.lpush(QUEUE_NAME, json.dumps(job))

    return {"ok": True, "enqueued": True, "meeting_id": meeting_id}

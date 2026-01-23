import json
import os
import re
from flask import Flask, jsonify, render_template, request
import requests

app = Flask(__name__)

AUTH_BASE_URL = os.environ.get("AUTH_BASE_URL", "https://mymeetai.site")
AUTH_TOKEN_ENDPOINT = os.environ.get("AUTH_TOKEN_ENDPOINT", "/token/meeting-sdk-jwt")
AUTH_API_KEY = os.environ.get("AUTH_API_KEY", "")
AUTH_HEADER = os.environ.get("AUTH_HEADER", "")
BOT_BASE_URL = os.environ.get("BOT_BASE_URL", "http://bot:3667")
BOT_DISPLAY_NAME = os.environ.get("BOT_DISPLAY_NAME", "Renins Bot")

MEETING_URL_RE = re.compile(r"https?://[^/]+/j/(?P<meeting>\d+)(\?[^#]+)?")
PWD_RE = re.compile(r"pwd=([^&]+)")
JWT_RE = re.compile(r"[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}\\.[A-Za-z0-9_-]{10,}")


def parse_meeting_url(url: str):
    match = MEETING_URL_RE.search(url)
    if not match:
        return None, None
    meeting_number = match.group("meeting")
    pwd_match = PWD_RE.search(url)
    pwd = pwd_match.group(1) if pwd_match else None
    return meeting_number, pwd


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/status")
def status():
    url = f"{BOT_BASE_URL}/api/v1/status"
    for _ in range(2):
        try:
            resp = requests.get(url, timeout=5)
            return jsonify(resp.json())
        except (requests.ConnectionError, requests.Timeout):
            continue
        except requests.RequestException:
            break
    return jsonify({"ok": False, "error": "BOT_UNAVAILABLE", "url": url}), 502


@app.route("/api/diag")
def diag():
    return jsonify(
        {
            "auth_token_url": f"{AUTH_BASE_URL}{AUTH_TOKEN_ENDPOINT}",
            "bot_status_url": f"{BOT_BASE_URL}/api/v1/status",
            "bot_join_url": f"{BOT_BASE_URL}/api/v1/join",
            "bot_leave_url": f"{BOT_BASE_URL}/api/v1/leave",
            "env": {
                "AUTH_BASE_URL": AUTH_BASE_URL,
                "AUTH_TOKEN_ENDPOINT": AUTH_TOKEN_ENDPOINT,
                "BOT_BASE_URL": BOT_BASE_URL,
                "BOT_DISPLAY_NAME": BOT_DISPLAY_NAME,
            },
        }
    )


def mask_jwts(text: str) -> str:
    if not text:
        return ""
    return JWT_RE.sub("[REDACTED_JWT]", text)


def build_auth_headers():
    headers = {"Accept": "application/json"}
    if AUTH_HEADER:
        headers["Authorization"] = AUTH_HEADER
    elif AUTH_API_KEY:
        headers["Authorization"] = f"Bearer {AUTH_API_KEY}"
    return headers


def fetch_meeting_sdk_jwt():
    headers = {"Accept": "application/json"}
    headers.update(build_auth_headers())
    url = f"{AUTH_BASE_URL}{AUTH_TOKEN_ENDPOINT}"
    try:
        resp = requests.get(url, headers=headers, timeout=10)
        resp.raise_for_status()
    except requests.RequestException:
        return None, "AUTH_UNAVAILABLE"

    try:
        data = resp.json()
    except ValueError:
        return None, "INVALID_AUTH_RESPONSE"

    for key in ("token", "sdk_jwt", "meeting_sdk_jwt", "jwt"):
        token = data.get(key)
        if token:
            return token, None
    return None, "INVALID_AUTH_RESPONSE"


@app.route("/api/start", methods=["POST"])
def start():
    data = request.get_json() or {}
    meeting_url = data.get("meeting_url")
    passcode = data.get("passcode")

    if not meeting_url:
        return jsonify({"error": "meeting_url_required"}), 400

    meeting_number, pwd = parse_meeting_url(meeting_url)
    if not meeting_number:
        return jsonify({"error": "invalid_meeting_url"}), 400

    if passcode:
        pwd = passcode

    auth_url = f"{AUTH_BASE_URL}{AUTH_TOKEN_ENDPOINT}"
    headers = build_auth_headers()
    try:
        auth_resp = requests.get(auth_url, headers=headers, timeout=10)
        status_code = auth_resp.status_code
        auth_resp.raise_for_status()
    except requests.RequestException as exc:
        snippet = ""
        if "auth_resp" in locals() and getattr(auth_resp, "text", None):
            snippet = mask_jwts(auth_resp.text[:300])
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "AUTH_ERROR",
                    "auth_url": auth_url,
                    "status_code": status_code if "status_code" in locals() else None,
                    "details": str(exc),
                    "response_snippet": snippet,
                }
            ),
            502,
        )

    try:
        auth_json = auth_resp.json()
    except ValueError:
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "AUTH_ERROR",
                    "auth_url": auth_url,
                    "status_code": auth_resp.status_code,
                    "details": "invalid JSON",
                    "response_snippet": mask_jwts(auth_resp.text[:300]),
                }
            ),
            502,
        )

    token = (
        auth_json.get("token")
        or auth_json.get("sdk_jwt")
        or auth_json.get("meeting_sdk_jwt")
        or auth_json.get("jwt")
    )
    if not token:
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "AUTH_ERROR",
                    "auth_url": auth_url,
                    "status_code": auth_resp.status_code,
                    "details": "token field not found",
                    "response_snippet": mask_jwts(json.dumps(auth_json)[:300]),
                }
            ),
            502,
        )

    payload = {
        "meeting_url": meeting_url,
        "passcode": pwd,
        "display_name": BOT_DISPLAY_NAME,
        "auth": {"sdk_auth_token": token},
    }

    try:
        bot_url = f"{BOT_BASE_URL}/api/v1/join"
        bot_resp = requests.post(bot_url, json=payload, timeout=10)
        bot_status = bot_resp.status_code
        bot_resp.raise_for_status()
    except requests.RequestException as exc:
        snippet = ""
        if "bot_resp" in locals() and getattr(bot_resp, "text", None):
            snippet = mask_jwts(bot_resp.text[:300])
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "BOT_JOIN_ERROR",
                    "bot_url": bot_url,
                    "status_code": bot_status if "bot_status" in locals() else None,
                    "details": str(exc),
                    "response_snippet": snippet,
                }
            ),
            502,
        )
    return jsonify({"ok": True})


@app.route("/api/test-auth")
def test_auth():
    auth_url = f"{AUTH_BASE_URL}{AUTH_TOKEN_ENDPOINT}"
    headers = build_auth_headers()
    try:
        auth_resp = requests.get(auth_url, headers=headers, timeout=10)
        status_code = auth_resp.status_code
        auth_resp.raise_for_status()
    except requests.RequestException as exc:
        snippet = ""
        if "auth_resp" in locals() and getattr(auth_resp, "text", None):
            snippet = mask_jwts(auth_resp.text[:300])
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "AUTH_ERROR",
                    "auth_url": auth_url,
                    "status_code": status_code if "status_code" in locals() else None,
                    "details": str(exc),
                    "response_snippet": snippet,
                }
            ),
            502,
        )

    try:
        auth_json = auth_resp.json()
    except ValueError:
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "AUTH_ERROR",
                    "auth_url": auth_url,
                    "status_code": auth_resp.status_code,
                    "details": "invalid JSON",
                    "response_snippet": mask_jwts(auth_resp.text[:300]),
                }
            ),
            502,
        )

    token = (
        auth_json.get("token")
        or auth_json.get("sdk_jwt")
        or auth_json.get("meeting_sdk_jwt")
        or auth_json.get("jwt")
    )
    return jsonify(
        {
            "ok": True,
            "auth_url": auth_url,
            "token_present": bool(token),
            "token_prefix": token[:12] if token else "",
            "keys": list(auth_json.keys()),
        }
    )


@app.route("/api/stop", methods=["POST"])
def stop():
    try:
        bot_resp = requests.post(f"{BOT_BASE_URL}/api/v1/leave", json={}, timeout=10)
        bot_resp.raise_for_status()
        return jsonify({"ok": True})
    except requests.RequestException:
        return jsonify({"ok": False, "error": "BOT_UNAVAILABLE"}), 502


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)

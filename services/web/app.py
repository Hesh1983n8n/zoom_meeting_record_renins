import os
import re
from flask import Flask, jsonify, render_template, request
import requests

app = Flask(__name__)

AUTH_BASE_URL = os.environ.get("AUTH_BASE_URL", "https://mymeetai.site")
AUTH_TOKEN_ENDPOINT = os.environ.get("AUTH_TOKEN_ENDPOINT", "/token/meeting-sdk-jwt")
AUTH_API_KEY = os.environ.get("AUTH_API_KEY", "")
BOT_BASE_URL = os.environ.get("BOT_BASE_URL", "http://bot:3667")
BOT_DISPLAY_NAME = os.environ.get("BOT_DISPLAY_NAME", "Renins Bot")

MEETING_URL_RE = re.compile(r"https?://[^/]+/j/(?P<meeting>\d+)(\?[^#]+)?")
PWD_RE = re.compile(r"pwd=([^&]+)")


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
    try:
        resp = requests.get(f"{BOT_BASE_URL}/api/v1/status", timeout=5)
        return jsonify(resp.json())
    except requests.RequestException:
        return jsonify({"ok": False, "error": "BOT_UNAVAILABLE"}), 502


def fetch_meeting_sdk_jwt():
    headers = {"Accept": "application/json"}
    if AUTH_API_KEY:
        headers["Authorization"] = f"Bearer {AUTH_API_KEY}"
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

    for key in ("token", "sdk_jwt", "meeting_sdk_jwt"):
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

    sdk_jwt, error = fetch_meeting_sdk_jwt()
    if error:
        return jsonify({"ok": False, "error": error}), 502

    payload = {
        "meeting_url": meeting_url,
        "passcode": pwd,
        "display_name": BOT_DISPLAY_NAME,
        "auth": {"sdk_auth_token": sdk_jwt},
    }

    try:
        bot_resp = requests.post(f"{BOT_BASE_URL}/api/v1/join", json=payload, timeout=10)
        bot_resp.raise_for_status()
    except requests.RequestException:
        return jsonify({"ok": False, "error": "BOT_UNAVAILABLE"}), 502
    return jsonify({"ok": True})


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

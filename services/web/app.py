import os
import re
from flask import Flask, jsonify, render_template, request
import requests

app = Flask(__name__)

AUTH_BASE_URL = os.environ.get("AUTH_BASE_URL", "https://mymeetai.site")
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
        return jsonify({"state": "error", "error": "bot_unreachable"}), 502


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

    token_resp = requests.post(
        f"{AUTH_BASE_URL}/api/zoom/meeting-sdk-token",
        headers={"Authorization": f"Bearer {AUTH_API_KEY}"},
        json={"meeting_number": meeting_number},
        timeout=10,
    )
    token_resp.raise_for_status()
    token_data = token_resp.json()

    payload = {
        "meeting_url": meeting_url,
        "passcode": pwd,
        "display_name": BOT_DISPLAY_NAME,
        "sdk_auth_token": token_data.get("sdk_auth_token"),
        "recording_token": token_data.get("recording_token"),
    }

    bot_resp = requests.post(f"{BOT_BASE_URL}/api/v1/join", json=payload, timeout=10)
    bot_resp.raise_for_status()
    return jsonify({"status": "ok"})


@app.route("/api/stop", methods=["POST"])
def stop():
    try:
        bot_resp = requests.post(f"{BOT_BASE_URL}/api/v1/leave", json={}, timeout=10)
        bot_resp.raise_for_status()
        return jsonify({"status": "ok"})
    except requests.RequestException:
        return jsonify({"error": "bot_unreachable"}), 502


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8080)

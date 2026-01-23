import json
import logging
import os
import subprocess
from pathlib import Path
from typing import Any, Dict

import requests


def ensure_dbus_session(env: Dict[str, str]) -> None:
    if env.get("DBUS_SESSION_BUS_ADDRESS"):
        return
    try:
        result = subprocess.run(
            ["dbus-daemon", "--session", "--fork", "--print-address=1", "--print-pid=1"],
            check=True,
            capture_output=True,
            text=True,
        )
        lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if lines:
            env["DBUS_SESSION_BUS_ADDRESS"] = lines[0]
            logging.info("dbus_session_started address=%s", lines[0])
    except Exception:
        logging.exception("dbus_session_start_failed")


def ensure_pulseaudio(env: Dict[str, str]) -> None:
    runtime_dir = env.get("XDG_RUNTIME_DIR", "/tmp/xdg")
    Path(runtime_dir).mkdir(parents=True, exist_ok=True)
    env["XDG_RUNTIME_DIR"] = runtime_dir
    try:
        subprocess.run(
            ["pulseaudio", "--start", "--exit-idle-time=-1", "--log-level=error"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        logging.info("pulseaudio_started")
    except Exception:
        logging.exception("pulseaudio_start_failed")


def fetch_meeting_sdk_signature() -> str:
    oauth_base_url = os.getenv("OAUTH_BASE_URL", "").rstrip("/")
    api_key = os.getenv("MEETAI_API_KEY", "")
    if not oauth_base_url or not api_key:
        raise RuntimeError("Missing OAUTH_BASE_URL or MEETAI_API_KEY")
    url = f"{oauth_base_url}/token/meeting-sdk-jwt"
    response = requests.get(url, headers={"X-API-Key": api_key}, timeout=20)
    if response.status_code != 200:
        logging.error(
            "signature_fetch_failed status=%s body=%s",
            response.status_code,
            response.text,
        )
        raise RuntimeError("Failed to fetch meeting SDK signature")
    payload = response.json()
    signature = payload.get("signature")
    if not signature:
        raise RuntimeError("Signature missing in OAuth response")
    return signature


def process_job(job: Dict[str, Any]) -> None:
    meeting_id = str(job.get("meeting_id"))
    passcode = job.get("passcode")
    display_name = job.get("display_name", "Meet.Ai")
    signature = job.get("signature")
    mode = job.get("mode", "per_user")

    data_dir = os.getenv("DATA_DIR", "/data")
    meeting_dir = Path(data_dir) / "meetings" / meeting_id
    meeting_dir.mkdir(parents=True, exist_ok=True)

    recorder_path = Path("/app/bin/zoom_bot_recorder")
    if not recorder_path.exists():
        raise FileNotFoundError("zoom_bot_recorder not found at /app/bin/zoom_bot_recorder")

    if not signature:
        try:
            signature = fetch_meeting_sdk_signature()
        except RuntimeError:
            logging.exception("job_failed signature_fetch_error meeting_id=%s", meeting_id)
            return

    logging.info("join_meeting meeting_id=%s display_name=%s mode=%s", meeting_id, display_name, mode)

    subprocess.run(
        ["sh", "-c", "rm -rf /tmp/meetai_home && mkdir -p /tmp/meetai_home"],
        check=False,
    )

    env = os.environ.copy()
    env["DISPLAY"] = env.get("DISPLAY", ":99")
    env["HOME"] = "/tmp/meetai_home"
    ensure_dbus_session(env)
    ensure_pulseaudio(env)

    subprocess.Popen(
        ["Xvfb", env["DISPLAY"], "-screen", "0", "1280x720x24"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    cmd = [
        str(recorder_path),
        "--meeting_id",
        meeting_id,
        "--display_name",
        display_name,
        "--signature",
        signature or "",
        "--out_dir",
        str(meeting_dir),
        "--mode",
        mode,
    ]

    if passcode:
        cmd.extend(["--passcode", passcode])

    logging.info("record_audio cmd=%s", json.dumps(cmd))
    subprocess.run(cmd, check=False, env=env)

    merge_cmd = ["/app/bin/merge_audio", str(meeting_dir)]
    logging.info("merge_audio cmd=%s", json.dumps(merge_cmd))
    subprocess.run(merge_cmd, check=False)

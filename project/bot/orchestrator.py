import json
import logging
import os
import subprocess
from pathlib import Path
from typing import Any, Dict


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

    logging.info("join_meeting meeting_id=%s display_name=%s mode=%s", meeting_id, display_name, mode)

    subprocess.run(
        ["sh", "-c", "rm -rf /tmp/meetai_home && mkdir -p /tmp/meetai_home"],
        check=False,
    )

    env = os.environ.copy()
    env["DISPLAY"] = env.get("DISPLAY", ":99")
    env["HOME"] = "/tmp/meetai_home"

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

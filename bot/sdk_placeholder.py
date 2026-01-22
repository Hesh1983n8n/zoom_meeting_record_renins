from __future__ import annotations

import re
import uuid
import logging
from pathlib import Path
from urllib.parse import parse_qs, urlparse


def _parse_meeting_url(meeting_url: str) -> tuple[str, str | None]:
    parsed = urlparse(meeting_url)
    meeting_id_match = re.search(r"/j/(\\d+)", parsed.path)
    meeting_id = meeting_id_match.group(1) if meeting_id_match else str(uuid.uuid4())
    query = parse_qs(parsed.query)
    passcode = query.get("pwd", [None])[0]
    return meeting_id, passcode


def start_zoom_meeting_record(
    meeting_url: str,
    display_name: str,
    passcode: str | None,
    record_dir: Path,
) -> tuple[str, str | None]:
    """
    Placeholder для интеграции с Zoom Meeting SDK.

    Шаги, которые нужно реализовать:
    1. Инициализировать SDK и передать SDK_KEY / SDK_SECRET.
    2. Подключиться к встрече по meeting_id + passcode.
    3. Подписаться на события участников (user_id → display_name).
    4. Подписаться на Raw Audio callback и писать WAV по каждому user_id.
    5. Закрыть запись при окончании встречи.
    """
    logger = logging.getLogger("zoom-bot")
    meeting_id, parsed_passcode = _parse_meeting_url(meeting_url)
    resolved_passcode = passcode or parsed_passcode
    meeting_path = record_dir / meeting_id
    meeting_path.mkdir(parents=True, exist_ok=True)
    logger.info("Initializing Zoom SDK (placeholder).")
    logger.info("Join meeting %s as %s (placeholder).", meeting_id, display_name)
    return meeting_id, resolved_passcode


def capture_audio_placeholder(meeting_id: str, record_dir: Path) -> None:
    logger = logging.getLogger("zoom-bot")
    logger.info("START capture_audio for meeting %s (placeholder).", meeting_id)
    logger.warning("SDK callbacks are not wired; no audio will be captured.")
    logger.info("OK capture_audio for meeting %s (placeholder).", meeting_id)

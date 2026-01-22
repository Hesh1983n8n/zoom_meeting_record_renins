from __future__ import annotations

import re
import uuid
from urllib.parse import parse_qs, urlparse


def parse_meeting_details(meeting_url: str, passcode: str | None) -> tuple[str, str | None]:
    parsed = urlparse(meeting_url)
    meeting_id_match = re.search(r"/j/(\d+)", parsed.path)
    meeting_id = meeting_id_match.group(1) if meeting_id_match else str(uuid.uuid4())
    query = parse_qs(parsed.query)
    parsed_passcode = query.get("pwd", [None])[0]
    resolved_passcode = passcode or parsed_passcode
    return meeting_id, resolved_passcode

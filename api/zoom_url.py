import re
from typing import Any, Dict, Optional
from urllib.parse import parse_qs, urlparse

MEETING_RE = re.compile(r"/j/(\d+)")


def parse_zoom_meeting_url(meeting_url: str) -> Dict[str, Any]:
    """
    Returns:
      meeting_id: str
      pwd_token: Optional[str]   # query param ?pwd=... (NOT passcode)
    """
    meeting_url = meeting_url.strip()
    parsed = urlparse(meeting_url)

    match = MEETING_RE.search(parsed.path or "")
    if not match:
        raise ValueError("Could not parse meeting_id from meeting_url")

    meeting_id = match.group(1)
    qs = parse_qs(parsed.query or "")
    pwd_token: Optional[str] = qs.get("pwd", [None])[0]

    return {"meeting_id": meeting_id, "pwd_token": pwd_token}

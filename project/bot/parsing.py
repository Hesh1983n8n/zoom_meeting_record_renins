import json
from typing import Any, Dict


class JobParseError(ValueError):
    pass


def parse_job(raw: str) -> Dict[str, Any]:
    try:
        job = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise JobParseError("Invalid job payload") from exc

    if "meeting_id" not in job:
        raise JobParseError("Job missing meeting_id")

    return job

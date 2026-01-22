import logging
import os
import traceback

import redis

from orchestrator import process_job
from parsing import JobParseError, parse_job


def configure_logging() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )


def main() -> None:
    configure_logging()
    redis_url = os.getenv("REDIS_URL", "redis://redis:6379/0")
    queue_name = os.getenv("QUEUE_NAME", "zoom_jobs")
    logging.info("worker_start redis_url=%s queue=%s", redis_url, queue_name)

    client = redis.Redis.from_url(redis_url, decode_responses=True)

    while True:
        try:
            _, payload = client.blpop(queue_name)
            logging.info("job_received payload=%s", payload)
            job = parse_job(payload)
            process_job(job)
        except JobParseError as exc:
            logging.error("job_parse_error %s", exc)
        except Exception:
            logging.error("job_failed %s", traceback.format_exc())


if __name__ == "__main__":
    main()

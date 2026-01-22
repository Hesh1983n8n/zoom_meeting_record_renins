FROM python:3.11-slim

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        ffmpeg \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY worker.py orchestrator.py parsing.py /app/
COPY tools/ /app/tools/
COPY native/ /app/native/
COPY zoom_sdk/ /app/zoom_sdk/

RUN cmake -S /app/native -B /app/native/build \
    && cmake --build /app/native/build --config Release \
    && mkdir -p /app/bin \
    && cp /app/native/build/zoom_bot_recorder /app/bin/zoom_bot_recorder \
    && cp /app/tools/merge_audio.sh /app/bin/merge_audio

RUN ls -lah /app/zoom_sdk/lib \
    && ldd /app/bin/zoom_bot_recorder | tee /tmp/ldd.txt \
    && ! grep -q "not found" /tmp/ldd.txt

RUN chmod +x /app/bin/merge_audio /app/bin/zoom_bot_recorder \
    && test -x /app/bin/zoom_bot_recorder

ENV LD_LIBRARY_PATH=/app/zoom_sdk/lib:${LD_LIBRARY_PATH}

CMD ["python", "/app/worker.py"]

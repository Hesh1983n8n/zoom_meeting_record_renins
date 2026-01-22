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

RUN echo "=== LIST /app/zoom_sdk ===" \
    && ls -lah /app/zoom_sdk || true \
    && echo "=== LIST /app/zoom_sdk/include ===" \
    && ls -lah /app/zoom_sdk/include || true \
    && echo "=== LIST /app/zoom_sdk/lib ===" \
    && ls -lah /app/zoom_sdk/lib || true \
    && echo "=== FIND SDK FILES (h/so/a) ===" \
    && find /app/zoom_sdk -maxdepth 4 -type f \( -name "*.h" -o -name "*.so" -o -name "*.a" \) | head -n 200

RUN echo "=== RAW/AUDIO HEADERS ===" \
    && find /app/zoom_sdk/include -maxdepth 4 -type f -iname "*raw*" -o -iname "*audio*" | head -n 200

RUN grep -R "IZoomSDKAudioRawDataHelper" -n /app/zoom_sdk/include | head -n 50 || true
RUN grep -R "rawdata_audio_helper_interface" -n /app/zoom_sdk/include | head -n 50 || true
RUN grep -R "tagJoinParam" -n /app/zoom_sdk/include/meeting_service_interface.h | head -n 50 || true
RUN grep -R "JoinParam4WithoutLogin" -n /app/zoom_sdk/include/meeting_service_interface.h | head -n 50 || true
RUN grep -R "GetAudio.*Raw" -n /app/zoom_sdk/include | head -n 50 || true
RUN grep -R "AudioRawDataHelper" -n /app/zoom_sdk/include | head -n 50 || true
RUN set -eux; \
  echo "=== JOIN PARAM UNION FIELDS (grep) ==="; \
  grep -n "tagJoinParam" -n /app/zoom_sdk/include/meeting_service_interface.h | head -n 60 || true; \
  echo "=== JoinParam4WithoutLogin occurrences ==="; \
  grep -n "JoinParam4WithoutLogin" /app/zoom_sdk/include/meeting_service_interface.h | head -n 80 || true; \
  echo "=== show tagJoinParam struct block (best effort) ==="; \
  awk 'NR>=1 && NR<=1200 {print}' /app/zoom_sdk/include/meeting_service_interface.h | grep -n "tagJoinParam" -n | head -n 20 || true; \
  echo "=== RAWDATA helper symbols ==="; \
  grep -R "AudioRawDataHelper" -n /app/zoom_sdk/include | head -n 80 || true; \
  grep -R "Get.*Raw.*Data.*Helper" -n /app/zoom_sdk/include | head -n 80 || true; \
  grep -R "Create.*Raw.*Data" -n /app/zoom_sdk/include | head -n 80 || true;

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

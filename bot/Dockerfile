FROM python:3.11-slim

WORKDIR /app

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ffmpeg \
        build-essential \
        cmake \
        pkg-config \
        ca-certificates \
        xvfb \
        xauth \
        dbus \
        dbus-x11 \
        libx11-6 \
        libx11-xcb1 \
        libglib2.0-0 \
        libnss3 \
        libasound2 \
        libpulse0 \
        libpulse-mainloop-glib0 \
        qtbase5-dev \
        libxcb1 \
        libxcb-icccm4 \
        libxcb-image0 \
        libxcb-keysyms1 \
        libxcb-randr0 \
        libxcb-render0 \
        libxcb-render-util0 \
        libxcb-shm0 \
        libxcb-xtest0 \
        libxcb-util1 \
        libxcb-xinerama0 \
        libxfixes3 \
        libxi6 \
        libxkbcommon-x11-0 \
        libxrandr2 \
        libxrender1 \
        libxtst6 \
        pulseaudio \
    && rm -rf /var/lib/apt/lists/*

COPY requirements.txt /app/requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

COPY worker.py orchestrator.py parsing.py /app/
COPY tools/ /app/tools/
COPY native/ /app/native/
COPY zoom_sdk/ /app/zoom_sdk/

RUN set -eux; \
  cd /app/zoom_sdk/lib; \
  if [ -f libmeetingsdk.so ] && [ ! -e libmeetingsdk.so.1 ]; then \
    ln -s libmeetingsdk.so libmeetingsdk.so.1; \
  fi; \
  for f in libQt5Core.so.5 libQt5Network.so.5 libQt5DBus.so.5 libQt5Gui.so.5 libQt5Widgets.so.5 libQt5XcbQpa.so.5 libQt5X11Extras.so.5; do \
    base="${f%.5}"; \
    if [ -e "$f" ] && [ ! -e "$base" ]; then \
      ln -s "$f" "$base"; \
    fi; \
  done

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

RUN echo "=== VERIFY join/audio identifiers ===" \
    && grep -n "withoutloginuserJoin" /app/native/main.cpp \
    && grep -n "GetAudioRawdataHelper" /app/native/main.cpp \
    && ! grep -n "GetJoinWithoutLogin" /app/native/main.cpp || true \
    && ! grep -n "without_login" /app/native/main.cpp || true

RUN cmake -S /app/native -B /app/native/build \
    && cmake --build /app/native/build --config Release \
    && mkdir -p /app/bin \
    && cp /app/native/build/zoom_bot_recorder /app/bin/zoom_bot_recorder \
    && cp /app/tools/merge_audio.sh /app/bin/merge_audio

RUN ls -lah /app/zoom_sdk/lib \
    && ldd /app/bin/zoom_bot_recorder | tee /tmp/ldd.txt \
    && ! grep -q "not found" /tmp/ldd.txt

RUN ldd /app/zoom_sdk/lib/libmeetingsdk.so | tee /tmp/meetingsdk-ldd.txt

RUN chmod +x /app/bin/merge_audio /app/bin/zoom_bot_recorder \
    && test -x /app/bin/zoom_bot_recorder

ENV HOME=/tmp/meetai_home
ENV LD_LIBRARY_PATH=/app/zoom_sdk/lib:${LD_LIBRARY_PATH}

CMD ["python", "/app/worker.py"]

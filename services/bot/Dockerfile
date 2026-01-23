FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates pkg-config \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY scripts/validate_sdk_layout.sh /app/scripts/validate_sdk_layout.sh
COPY third_party/sdk/zoom-meeting-sdk-linux_x86_64-6.7.2.7020/ /opt/sdk/

RUN sed -i 's/\r$//' /app/scripts/validate_sdk_layout.sh \
  && chmod +x /app/scripts/validate_sdk_layout.sh

RUN /app/scripts/validate_sdk_layout.sh /opt/sdk

COPY CMakeLists.txt /app/CMakeLists.txt
COPY src/ /app/src/

RUN cmake -S /app -B /app/build -DSDK_ROOT=/opt/sdk -DCMAKE_VERBOSE_MAKEFILE=ON \
  && cmake --build /app/build -- VERBOSE=1

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl procps \
    libatomic1 \
    libx11-6 \
    libxcb1 \
    libxcb-xfixes0 \
    libxcb-shape0 \
    libxcb-shm0 \
    libxcb-randr0 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-xtest0 \
    libglib2.0-0 \
    libgbm1 \
    libdrm2 \
    libxfixes3 \
    libdbus-1-3 \
    libgl1 \
  && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/sdk /opt/sdk
COPY --from=build /app/build/zoom_bot /usr/local/bin/zoom_bot
COPY entrypoint.sh /entrypoint.sh

RUN chmod +x /entrypoint.sh

ENV LD_LIBRARY_PATH=/opt/sdk:/opt/sdk/qt_libs/Qt/lib:/opt/sdk/qt_libs
ENV RECORDS_DIR=/records
ENV BOT_PORT=3667

EXPOSE 3667

ENTRYPOINT ["/entrypoint.sh"]

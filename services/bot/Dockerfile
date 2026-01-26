FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates pkg-config \
    libx11-dev \
    libxcb1-dev \
    libxcb-xfixes0-dev \
    libxcb-shape0-dev \
    libxcb-shm0-dev \
    libxcb-randr0-dev \
    libxcb-image0-dev \
    libxcb-keysyms1-dev \
    libxcb-xtest0-dev \
    libxcb-util0-dev \
    libxfixes-dev \
    libdbus-1-dev \
    libgl1-mesa-dev \
    libgbm-dev \
    libdrm-dev \
    libglib2.0-dev \
    libatomic1 \
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
    ca-certificates curl pciutils procps \
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
    libxcb-util1 \
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

RUN echo "/opt/sdk" > /etc/ld.so.conf.d/zoom-sdk.conf && ldconfig

ENV LD_LIBRARY_PATH=/opt/sdk:/opt/sdk/qt_libs/Qt/lib:/opt/sdk/qt_libs
ENV RECORDS_DIR=/records
ENV BOT_PORT=3667

EXPOSE 3667

ENTRYPOINT ["/entrypoint.sh"]

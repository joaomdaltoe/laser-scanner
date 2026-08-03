FROM ubuntu:18.04

ARG DEBIAN_FRONTEND=noninteractive
ARG INSTALL_DEBUG_TOOLS=0
ARG CMAKE_BUILD_TYPE=Release

COPY third_party/flycapture2/*.deb /tmp/flycapture2/

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    qtbase5-dev \
    /tmp/flycapture2/libflycapture-2*.deb \
 && if [ "$INSTALL_DEBUG_TOOLS" = "1" ]; then \
      apt-get install -y --no-install-recommends gdb; \
    fi \
 && rm -rf /var/lib/apt/lists/* \
 && rm -rf /tmp/flycapture2   

WORKDIR  /opt/laser_scanner/

COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

RUN mkdir -p build \
 && cd build \
 && cmake -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" .. \
 && cmake --build .

CMD ["./build/laser_scanner"]

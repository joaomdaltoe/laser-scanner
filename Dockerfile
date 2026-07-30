FROM ubuntu:18.04

ARG DEBIAN_FRONTEND=noninteractive

COPY third_party/flycapture2/*.deb /tmp/flycapture2/

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    qtbase5-dev \
    /tmp/flycapture2/libflycapture-2*.deb \
 && rm -rf /var/lib/apt/lists/* \
 && rm -rf /tmp/flycapture2   

WORKDIR  /opt/laser_scanner/

COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

RUN mkdir -p build \
 && cd build \
 && cmake -DCMAKE_BUILD_TYPE=Release .. \
 && cmake --build .

CMD ["./build/laser_scanner"]

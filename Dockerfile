FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Base dependencies
RUN apt-get update && apt-get install -y \
    cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
    build-essential git python3 \
    libstdc++-arm-none-eabi-newlib \
    gdb-multiarch \
    libtool pkg-config autoconf automake texinfo libusb-1.0-0-dev \
    libjim-dev \
    libhidapi-dev \
    && rm -rf /var/lib/apt/lists/*

# Build Raspberry Pi OpenOCD
WORKDIR /opt
RUN git clone https://github.com/raspberrypi/openocd.git \
    && cd openocd \
    && ./bootstrap \
    && ./configure --enable-cmsis-dap \
    && make -j$(nproc) \
    && make install \
    && cd / && rm -rf /opt/openocd

# Set OpenOCD scripts path
ENV OPENOCD_SCRIPTS=/usr/local/share/openocd/scripts


# Pico SDK
WORKDIR /data
RUN git clone https://github.com/raspberrypi/pico-sdk.git \
    && cd pico-sdk && git submodule update --init

ENV PICO_SDK_PATH=/data/pico-sdk

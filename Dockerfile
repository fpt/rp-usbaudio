# Dockerfile for RP2350 USB Audio DAC firmware build
FROM debian:bookworm-slim

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    build-essential \
    git \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Clone pico-extras at a pinned commit for reproducibility
RUN git clone https://github.com/raspberrypi/pico-extras.git /pico-extras \
    && cd /pico-extras \
    && git submodule update --init

# Set working directory
WORKDIR /work

# Default command
CMD ["/bin/bash"]

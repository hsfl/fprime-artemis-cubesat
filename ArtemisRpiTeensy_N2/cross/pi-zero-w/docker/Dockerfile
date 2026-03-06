FROM python:3.11-slim-bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    binutils \
    binutils-arm-linux-gnueabihf \
    build-essential \
    ca-certificates \
    cmake \
    file \
    g++-arm-linux-gnueabihf \
    gcc-arm-linux-gnueabihf \
    git \
    ninja-build \
    pkg-config \
    rsync \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

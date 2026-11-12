FROM python:3.11-slim-bookworm@sha256:0a310eeecf4e1f5a0743f9a6520c90c88d089c903ca5fd283f501e3a805f5f89

ENV DEBIAN_FRONTEND=noninteractive \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    ZEPHYR_TOOLCHAIN_VARIANT=host

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential git cmake ninja-build device-tree-compiler ccache \
    libglib2.0-0 libgl1 libusb-1.0-0-dev pkg-config curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# The framework's release requirements own the Yamcs/FPP/tools versions.
COPY fprime-artemis-core/lib/fprime/requirements.txt /tmp/fprime-requirements.txt
COPY fprime-artemis-core/lib/zephyr-workspace/zephyr/scripts/requirements-base.txt /tmp/zephyr-requirements.txt
COPY twin/requirements.lock /tmp/runtime-requirements.lock
RUN python -m pip install --no-cache-dir -r /tmp/fprime-requirements.txt \
    -r /tmp/zephyr-requirements.txt -r /tmp/runtime-requirements.lock \
    && python -m pip check

WORKDIR /workspace
CMD ["python", "tools/twin/twin.py", "--help"]

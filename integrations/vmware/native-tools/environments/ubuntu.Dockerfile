# Preparation only; source-package performs the native build without networking.
FROM karyo-source-builder:ubuntu2604
ARG OPEN_VM_TOOLS_VERSION=2:13.0.10-1ubuntu1.1
RUN sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/*.sources \
    && apt-get update \
    && apt-get build-dep -y --no-install-recommends "open-vm-tools=$OPEN_VM_TOOLS_VERSION" \
    && apt-get install -y --no-install-recommends xvfb xauth python3-pil python3-xlib libxext-dev \
    && rm -rf /var/lib/apt/lists/*

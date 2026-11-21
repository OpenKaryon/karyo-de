# Build preparation only; actual source-package builds run without networking.
FROM karyo-source-builder:trixie
RUN sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get build-dep -y --no-install-recommends labwc=0.8.3-1 \
    && apt-get install -y --no-install-recommends wayland-utils \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends libcmocka-dev xwayland \
    && rm -rf /var/lib/apt/lists/*

# Native source preparation only; actual builds have no network.
FROM karyo-win32-qa:debian
USER root
RUN sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get build-dep -y --no-install-recommends wine=10.0~repack-6 \
    && mkdir /source-inputs && cd /source-inputs \
    && apt-get source --download-only --only-source wine=10.0~repack-6 \
    && rm -rf /var/lib/apt/lists/*

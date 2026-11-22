# Native Wine32 preparation. Build with --platform=linux/386.
FROM karyo-source-builder:trixie-i386
RUN test "$(dpkg --print-architecture)" = i386 \
    && sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get build-dep -y --no-install-recommends wine=10.0~repack-6 \
    && rm -rf /var/lib/apt/lists/*

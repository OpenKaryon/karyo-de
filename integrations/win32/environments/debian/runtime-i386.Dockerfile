# Disposable QA preparation only; never alters host or product APT sources.
FROM karyo-wine-source-builder:debian
USER root
RUN dpkg --add-architecture i386 \
    && apt-get update && apt-get install -y --no-install-recommends wine32:i386=10.0~repack-6 \
    && rm -rf /var/lib/apt/lists/*
# The native Wine64 SDK does not need an i686 compiler; our own PE32 fixture does.
RUN apt-get update && apt-get install -y --no-install-recommends gcc-mingw-w64-i686-posix \
    && command -v i686-w64-mingw32-windres && command -v i686-w64-mingw32-gcc \
    && rm -rf /var/lib/apt/lists/*

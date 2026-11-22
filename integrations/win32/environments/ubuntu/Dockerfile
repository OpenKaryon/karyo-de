# QA preparation only. Product packages create no accounts or repositories.
FROM karyo-compositor-builder:ubuntu2604
RUN apt-get update && apt-get install -y --no-install-recommends wine wine64 fonts-wine icoutils gcc-mingw-w64-x86-64-posix python3-pyqt6 desktop-file-utils shared-mime-info \
    && rm -rf /var/lib/apt/lists/*
RUN useradd --uid 1001 --create-home --shell /bin/sh win32qa
RUN apt-get update && apt-get install -y --no-install-recommends fonts-liberation wixl dbus-daemon dbus-bin \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 1002 --create-home --shell /bin/sh workqa

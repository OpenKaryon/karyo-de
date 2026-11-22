# Native SDK preparation only. Actual source builds have no network.
FROM karyo-compositor-builder:trixie
RUN apt-get update && apt-get build-dep -y --no-install-recommends wlroots=0.18.2-3 \
    && rm -rf /var/lib/apt/lists/*

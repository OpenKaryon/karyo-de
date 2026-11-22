# Native SDK preparation only. Actual source builds have no network.
FROM karyo-compositor-builder:ubuntu2604
RUN apt-get update && apt-get build-dep -y --no-install-recommends wlroots=0.19.2-1 \
    && rm -rf /var/lib/apt/lists/*

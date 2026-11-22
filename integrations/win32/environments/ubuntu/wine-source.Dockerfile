# Native source preparation only; actual builds have no network.
# Wine10's data generator needs Unicode16/CLDR46. No native ABI packages
# are selected from the older signed Ubuntu snapshot.
FROM karyo-win32-qa:ubuntu
USER root
RUN printf '%s\n' \
    'Types: deb' \
    'URIs: https://snapshot.ubuntu.com/ubuntu/20250417T000000Z' \
    'Suites: plucky' \
    'Components: universe' \
    'Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg' \
    'Check-Valid-Until: no' > /etc/apt/sources.list.d/wine-generator-data.sources \
    && printf '%s\n' 'Package: unicode-data unicode-idna unicode-cldr-core' \
       'Pin: release n=plucky' 'Pin-Priority: 1001' '' 'Package: *' \
       'Pin: release n=plucky' 'Pin-Priority: -1' > /etc/apt/preferences.d/wine-generator-data \
    && sed -i "s/^Types: deb$/Types: deb deb-src/" /etc/apt/sources.list.d/ubuntu.sources \
    && apt-get update && apt-cache policy unicode-data unicode-idna unicode-cldr-core \
    && apt-get build-dep -y --no-install-recommends wine=10.0~repack-12ubuntu1 \
    && mkdir /source-inputs && cd /source-inputs \
    && apt-get source --download-only --only-source wine=10.0~repack-12ubuntu1 \
    && rm -rf /var/lib/apt/lists/*

# Disposable QA only; never run a Windows binary as root.
ARG QA_IMAGE
FROM ${QA_IMAGE}
ARG WIN32_SHA256
ARG COMPOSITOR_SHA256
COPY karyo-win32.deb labwc.deb /tmp/
RUN echo "${WIN32_SHA256}  /tmp/karyo-win32.deb" | sha256sum -c - \
    && echo "${COMPOSITOR_SHA256}  /tmp/labwc.deb" | sha256sum -c - \
    && dpkg -i /tmp/karyo-win32.deb /tmp/labwc.deb \
    && dpkg -V karyo-win32 labwc \
    && rm /tmp/karyo-win32.deb /tmp/labwc.deb
WORKDIR /usr/share/karyo-qa
COPY runtime-probe.c runtime-probe.wxs probe.rc ./
RUN python3 -c 'from PIL import Image; Image.new("RGBA",(32,32),(255,128,0,255)).save("probe.ico",sizes=[(32,32)])' \
    && x86_64-w64-mingw32-windres probe.rc probe-resource.o \
    && x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -municode runtime-probe.c probe-resource.o -lgdi32 -o runtime-probe.exe \
    && wixl -a x64 -o runtime-probe.msi runtime-probe.wxs
RUN python3 -c 'import subprocess,pathlib; p=subprocess.run(["/usr/bin/karyo-win32","--gui","/usr/share/karyo-qa/runtime-probe.exe"],capture_output=True); assert p.returncode==1 and b"root" in p.stderr and not pathlib.Path("/root/.local/share/karyo/windows").exists()'
USER 1001:1001

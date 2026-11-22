# Disposable menu fixture; Wine itself never executes as root.
ARG INSTALLED_IMAGE
FROM ${INSTALLED_IMAGE}
USER root
COPY menu-probe.c /usr/share/karyo-qa/menu-probe.c
RUN x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -municode /usr/share/karyo-qa/menu-probe.c -o /usr/share/karyo-qa/menu-probe.exe
COPY pointer-probe.c protocols/wlr-virtual-pointer-unstable-v1.xml /usr/share/karyo-qa/
RUN cd /usr/share/karyo-qa \
    && wayland-scanner client-header wlr-virtual-pointer-unstable-v1.xml pointer.h \
    && wayland-scanner private-code wlr-virtual-pointer-unstable-v1.xml pointer.c \
    && cc -std=c11 -Wall -Wextra -Werror pointer-probe.c pointer.c $(pkg-config --cflags --libs wayland-client) -o pointer-probe
USER 1001:1001

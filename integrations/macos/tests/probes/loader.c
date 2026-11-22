/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A real Intel Mach-O executable: libSystem startup, file I/O and literal argv. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void json_string(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    putchar('"');
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') { putchar('\\'); putchar(*p); }
        else if (*p < 32) printf("\\u%04x", *p);
        else putchar(*p);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    const char payload[] = "Karyo: Привет, macOS!\n";
    char path[] = "/tmp/karyo-loader-XXXXXX";
    char data[sizeof(payload)] = {0};
    int fd = mkstemp(path);
    int passed = fd >= 0;
    if (passed) {
        passed = write(fd, payload, sizeof(payload)) == sizeof(payload)
            && lseek(fd, 0, SEEK_SET) == 0
            && read(fd, data, sizeof(data)) == sizeof(data)
            && memcmp(data, payload, sizeof(data)) == 0;
        close(fd);
        unlink(path);
    }
    printf("{\"schema\":1,\"probe\":\"macho-loader\",\"target\":\"macos\","
           "\"architecture\":\"x86_64\",\"passed\":%s,\"pid\":%ld,\"argv\":[",
           passed ? "true" : "false", (long)getpid());
    for (int i = 1; i < argc; ++i) { if (i != 1) putchar(','); json_string(argv[i]); }
    puts("]}");
    fflush(stdout);
    if (passed && argc == 2 && strcmp(argv[1], "--exit17") == 0) return 17;
    return !passed;
}

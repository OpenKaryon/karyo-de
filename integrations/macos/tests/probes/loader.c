/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A real Intel Mach-O executable: libSystem startup, file I/O and literal argv. */
#include <fcntl.h>
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

static int persistent_data(int writing, const char *name)
{
    static const char payload[] = "Karyo: Привет, preserved Mac application data!\n";
    const char *home = getenv("HOME");
    char path[4096] = {0};
    char data[sizeof(payload)] = {0};
    int passed = home && home[0] == '/' && name[0] && !strchr(name, '/');
    if (passed) {
        int length = snprintf(path, sizeof(path), "%s/%s", home, name);
        passed = length > 0 && (size_t)length < sizeof(path);
    }
    if (passed) {
        int fd = open(path, writing ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY, 0600);
        passed = fd >= 0;
        if (passed) {
            if (writing)
                passed = write(fd, payload, sizeof(payload) - 1) == sizeof(payload) - 1 && fsync(fd) == 0;
            else
                passed = read(fd, data, sizeof(data)) == sizeof(payload) - 1
                    && memcmp(data, payload, sizeof(payload) - 1) == 0;
            if (close(fd) != 0) passed = 0;
        }
    }
    printf("{\"schema\":1,\"probe\":\"macho-persistent-data\",\"target\":\"macos\","
           "\"architecture\":\"x86_64\",\"passed\":%s,\"mode\":\"%s\",\"path\":",
           passed ? "true" : "false", writing ? "write" : "read");
    json_string(path);
    printf(",\"payloadBytes\":%lu}\n", (unsigned long)(sizeof(payload) - 1));
    fflush(stdout);
    return !passed;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--write-data") == 0) return persistent_data(1, argv[2]);
    if (argc == 3 && strcmp(argv[1], "--read-data") == 0) return persistent_data(0, argv[2]);
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

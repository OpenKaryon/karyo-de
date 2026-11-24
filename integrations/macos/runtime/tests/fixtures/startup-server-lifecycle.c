/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary same-user service lifecycle; the child exits normally. */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>

char *prefix;
uid_t g_originalUid, g_originalGid;
#include "get-init-process.inc"

int main(void)
{
    char folder[] = "/tmp/karyo-startup-lifecycle-XXXXXX";
    prefix = mkdtemp(folder);
    if (!prefix) return 10;
    g_originalUid = getuid();
    g_originalGid = getgid();
    char pidfile[256], marker[256];
    snprintf(pidfile, sizeof(pidfile), "%s/.init.pid", prefix);
    snprintf(marker, sizeof(marker), "%s/ordinary-saved-data.txt", prefix);
    FILE *f = fopen(marker, "w");
    if (!f) return 11;
    fputs("Karyo ordinary preserved data\n", f);
    fclose(f);
    int first_launch = getInitProcess() == 0;
    int ready[2], finish[2];
    if (pipe(ready) || pipe(finish)) return 12;
    pid_t child = fork();
    if (child == -1) return 13;
    if (child == 0) {
        close(ready[0]);
        close(finish[1]);
        if (prctl(PR_SET_NAME, "darlingserver", 0, 0, 0) != 0) _exit(14);
        if (write(ready[1], "R", 1) != 1) _exit(15);
        char ch;
        if (read(finish[0], &ch, 1) != 1) _exit(16);
        _exit(0);
    }
    close(ready[1]);
    close(finish[0]);
    char ch;
    if (read(ready[0], &ch, 1) != 1) return 17;
    f = fopen(pidfile, "w");
    if (!f) return 18;
    fprintf(f, "%d\n", child);
    fclose(f);
    int live_server = getInitProcess() == child;
    if (write(finish[1], "F", 1) != 1) return 19;
    siginfo_t info;
    if (waitid(P_PID, child, &info, WEXITED | WNOWAIT) != 0) return 20;
    int normal_child_exit = info.si_code == CLD_EXITED && info.si_status == 0;
    int stale_server_ignored = getInitProcess() == 0;
    int stale_pid_file_removed = access(pidfile, F_OK) == -1 && errno == ENOENT;
    int status;
    if (waitpid(child, &status, 0) != child) return 21;
    close(ready[0]);
    close(finish[1]);
    char payload[128] = {0};
    f = fopen(marker, "r");
    if (!f) return 22;
    size_t n = fread(payload, 1, sizeof(payload) - 1, f);
    fclose(f);
    int data_preserved = n == strlen("Karyo ordinary preserved data\n") &&
        strcmp(payload, "Karyo ordinary preserved data\n") == 0;
    int prefix_preserved = access(prefix, F_OK) == 0;
    printf("{\"first_launch\":%s,\"live_server\":%s,\"normal_child_exit\":%s,"
           "\"stale_server_ignored\":%s,\"stale_pid_file_removed\":%s,"
           "\"data_preserved\":%s,\"prefix_preserved\":%s,\"intentional_crash\":false}\n",
           first_launch ? "true" : "false", live_server ? "true" : "false",
           normal_child_exit ? "true" : "false", stale_server_ignored ? "true" : "false",
           stale_pid_file_removed ? "true" : "false", data_preserved ? "true" : "false",
           prefix_preserved ? "true" : "false");
    unlink(pidfile);
    unlink(marker);
    rmdir(prefix);
    return !(first_launch && live_server && normal_child_exit && stale_server_ignored &&
             stale_pid_file_removed && data_preserved && prefix_preserved);
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern int ordinary_library_start(pthread_t *, void *(*)(void *), void *);
static sem_t ready, finished;
static int worker_chld, worker_ttou, worker_error;
static volatile sig_atomic_t child_notifications, receiver_tid;

static void child_changed(int signal_number)
{
    (void)signal_number;
    receiver_tid = (sig_atomic_t)syscall(SYS_gettid);
    child_notifications++;
}

static void *observe_worker(void *argument)
{
    (void)argument;
    sigset_t mask;
    worker_error = pthread_sigmask(SIG_SETMASK, NULL, &mask);
    worker_chld = sigismember(&mask, SIGCHLD);
    worker_ttou = sigismember(&mask, SIGTTOU);
    sem_post(&ready);
    while (sem_wait(&finished) != 0 && errno == EINTR) {}
    return NULL;
}

int main(void)
{
    if (getuid() == 0 || getuid() != geteuid())
        return 2;
    sigset_t before, after;
    if (pthread_sigmask(SIG_SETMASK, NULL, &before) != 0 ||
        sigismember(&before, SIGCHLD) != 0 ||
        sem_init(&ready, 0, 0) != 0 || sem_init(&finished, 0, 0) != 0)
        return 3;
    struct sigaction action = {0};
    action.sa_handler = child_changed;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGCHLD, &action, NULL) != 0)
        return 4;
    pthread_t thread;
    if (ordinary_library_start(&thread, observe_worker, NULL) != 0)
        return 5;
    while (sem_wait(&ready) != 0 && errno == EINTR) {}
    if (pthread_sigmask(SIG_SETMASK, NULL, &after) != 0)
        return 6;
    int preserved = 1;
    for (int number = 1; number < NSIG; number++)
        if (sigismember(&before, number) != sigismember(&after, number))
            preserved = 0;
    pid_t child = fork();
    if (child < 0)
        return 7;
    if (child == 0)
        _exit(0);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    for (int attempt = 0; child_notifications == 0 && attempt < 100; attempt++) {
        struct timespec delay = {0, 1000000};
        nanosleep(&delay, NULL);
    }
    sem_post(&finished);
    if (pthread_join(thread, NULL) != 0)
        return 8;
    printf("{\"native_library_worker_masks_both\":%s,"
           "\"parent_mask_preserved\":%s,\"ordinary_child_exited\":%s,"
           "\"ordinary_SIGCHLD_received\":%s,\"notification_on_main_thread\":%s}\n",
           worker_error == 0 && worker_chld == 1 && worker_ttou == 1 ? "true" : "false",
           preserved ? "true" : "false",
           waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? "true" : "false",
           child_notifications > 0 ? "true" : "false",
           receiver_tid == syscall(SYS_gettid) ? "true" : "false");
    sem_destroy(&ready);
    sem_destroy(&finished);
    return 0;
}

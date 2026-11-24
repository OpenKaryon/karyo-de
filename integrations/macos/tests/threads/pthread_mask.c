/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary Darwin pthread inheritance, join and Mach thread identity. */
#include <mach/mach.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>

struct thread_case {
    int mask_error;
    int chld;
    int ttou;
    mach_port_t thread_port;
    kern_return_t release_error;
};

static void *observe_thread(void *argument)
{
    struct thread_case *result = argument;
    sigset_t mask;
    result->mask_error = pthread_sigmask(SIG_SETMASK, NULL, &mask);
    if (result->mask_error == 0) {
        result->chld = sigismember(&mask, SIGCHLD);
        result->ttou = sigismember(&mask, SIGTTOU);
    }
    result->thread_port = mach_thread_self();
    if (result->thread_port != MACH_PORT_NULL)
        result->release_error = mach_port_deallocate(mach_task_self(), result->thread_port);
    return argument;
}

static int masks_equal(const sigset_t *left, const sigset_t *right)
{
    for (int number = 1; number < NSIG; ++number)
        if (sigismember(left, number) != sigismember(right, number))
            return 0;
    return 1;
}

int main(void)
{
    sigset_t original;
    int initial_error = pthread_sigmask(SIG_SETMASK, NULL, &original);
    if (initial_error != 0) {
        printf("{\"probe\":\"darwin-pthread-mask\",\"passed\":false,\"initialMaskError\":%d}\n", initial_error);
        return 1;
    }
    mach_port_t main_port = mach_thread_self();
    int passed = main_port != MACH_PORT_NULL;
    printf("{\"probe\":\"darwin-pthread-mask\",\"mainThreadPort\":%u,\"cases\":[", main_port);
    for (unsigned index = 0; index < 4; ++index) {
        sigset_t desired = original, after;
        sigdelset(&desired, SIGCHLD);
        sigdelset(&desired, SIGTTOU);
        if (index & 1) sigaddset(&desired, SIGCHLD);
        if (index & 2) sigaddset(&desired, SIGTTOU);
        int set_error = pthread_sigmask(SIG_SETMASK, &desired, NULL);
        struct thread_case result = {0};
        pthread_t thread;
        void *joined = NULL;
        int create_error = set_error ? set_error : pthread_create(&thread, NULL, observe_thread, &result);
        int join_error = create_error ? create_error : pthread_join(thread, &joined);
        int query_error = pthread_sigmask(SIG_SETMASK, NULL, &after);
        int parent_preserved = query_error == 0 && masks_equal(&desired, &after);
        int valid = set_error == 0 && create_error == 0 && join_error == 0 &&
            joined == &result && result.mask_error == 0 &&
            result.chld == (int)(index & 1) && result.ttou == (int)((index >> 1) & 1) &&
            result.thread_port != MACH_PORT_NULL && result.thread_port != main_port &&
            result.release_error == KERN_SUCCESS && parent_preserved;
        passed = passed && valid;
        printf("%s{\"chldBlocked\":%u,\"ttouBlocked\":%u,\"actualChld\":%d,\"actualTtou\":%d,"
               "\"setError\":%d,\"createError\":%d,\"joinError\":%d,\"maskError\":%d,"
               "\"threadPort\":%u,\"portReleaseError\":%d,\"parentMaskPreserved\":%s,\"passed\":%s}",
               index ? "," : "", index & 1, (index >> 1) & 1,
               result.chld, result.ttou, set_error, create_error, join_error,
               result.mask_error, result.thread_port, result.release_error,
               parent_preserved ? "true" : "false", valid ? "true" : "false");
    }
    int restore_error = pthread_sigmask(SIG_SETMASK, &original, NULL);
    sigset_t restored;
    int restored_ok = restore_error == 0 && pthread_sigmask(SIG_SETMASK, NULL, &restored) == 0 &&
        masks_equal(&original, &restored);
    kern_return_t main_release = main_port == MACH_PORT_NULL ? KERN_INVALID_NAME :
        mach_port_deallocate(mach_task_self(), main_port);
    passed = passed && restored_ok && main_release == KERN_SUCCESS;
    printf("],\"originalMaskRestored\":%s,\"mainPortReleaseError\":%d,\"passed\":%s}\n",
           restored_ok ? "true" : "false", main_release, passed ? "true" : "false");
    fflush(stdout);
    return passed ? 0 : 1;
}

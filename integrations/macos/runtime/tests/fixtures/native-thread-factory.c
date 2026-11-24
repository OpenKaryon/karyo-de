/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
int ordinary_library_start(pthread_t *thread, void *(*entry)(void *), void *argument)
{
    return pthread_create(thread, NULL, entry, argument);
}

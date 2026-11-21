// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed-service PAM password probe, restricted to disposable QA fixture users.
#define _GNU_SOURCE
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct conversation_data
{
    char password[1026];
    char current[1026];
    int ordinary;
    unsigned int prompts;
};

static int converse(
    int count, const struct pam_message **messages, struct pam_response **result, void *opaque)
{
    struct conversation_data *data = opaque;
    if (count <= 0 || count > 8)
        return PAM_CONV_ERR;
    struct pam_response *responses = calloc((size_t)count, sizeof(*responses));
    if (!responses)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i)
    {
        switch (messages[i]->msg_style)
        {
        case PAM_PROMPT_ECHO_OFF:
            if (++data->prompts > 4)
                goto fail;
            responses[i].resp = strdup(data->ordinary && data->prompts == 1 ? data->current : data->password);
            if (!responses[i].resp)
                goto fail;
            break;
        case PAM_ERROR_MSG:
        case PAM_TEXT_INFO:
            // PAM messages may contain privileged details; do not print them.
            break;
        default:
            goto fail;
        }
    }
    *result = responses;
    return PAM_SUCCESS;
fail:
    for (int i = 0; i < count; ++i)
    {
        if (responses[i].resp)
        {
            explicit_bzero(responses[i].resp, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    free(responses);
    return PAM_CONV_ERR;
}

int main(int argc, char **argv)
{
    char hostname[128] = { 0 };
    if ((argc != 2 && argc != 3) || getuid() != 0 || geteuid() != 0
        || gethostname(hostname, sizeof(hostname) - 1))
        return 2;
    if (strcmp(hostname, "karyo-de-acceptance") && strcmp(hostname, "karyo-de-ubuntu-acceptance"))
        return 2;
    if (strcmp(argv[1], "kq-pam-owner") && strcmp(argv[1], "kq-pam-shared")
        && strcmp(argv[1], "kq-pam-separate"))
        return 2;
    struct conversation_data data = { 0 };
    data.ordinary = argc == 3;
    if (data.ordinary && strcmp(argv[2], "--as-fixture-user"))
        return 2;
    if (data.ordinary)
    {
        if (!fgets(data.current, sizeof(data.current), stdin))
            return 2;
        size_t current_length = strlen(data.current);
        if (current_length < 9 || data.current[current_length - 1] != '\n')
            return 2;
        data.current[current_length - 1] = '\0';
    }
    if (!fgets(data.password, sizeof(data.password), stdin))
        return 2;
    size_t length = strlen(data.password);
    if (length < 9 || data.password[length - 1] != '\n' || fgetc(stdin) != EOF)
        return 2;
    data.password[length - 1] = '\0';
    if (data.ordinary)
    {
        struct passwd *entry = getpwnam(argv[1]);
        if (!entry || entry->pw_uid < 1000 || entry->pw_uid >= 65534)
            return 2;
        uid_t uid = entry->pw_uid;
        // Match a setuid-root passwd client: an ordinary real caller with root
        // effective privileges for the native PAM password stack.
        if (setresuid(uid, 0, 0) || getuid() != uid || geteuid() != 0)
            return 2;
    }
    struct pam_conv conversation = { converse, &data };
    pam_handle_t *handle = NULL;
    int status = pam_start("karyo-qa-password", argv[1], &conversation, &handle);
    if (status == PAM_SUCCESS)
        status = pam_chauthtok(handle, PAM_SILENT);
    if (handle)
        pam_end(handle, status);
    explicit_bzero(&data, sizeof(data));
    if (status != PAM_SUCCESS)
    {
        fprintf(stderr, "PAM fixture password change failed with code %d\n", status);
        return 1;
    }
    puts("PASS native PAM password update completed");
    return 0;
}

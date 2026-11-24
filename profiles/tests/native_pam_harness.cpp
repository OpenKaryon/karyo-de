// SPDX-License-Identifier: GPL-3.0-or-later
// Disposable native acceptance only. Not installed and never setuid.
#include <security/pam_appl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <algorithm>

namespace {
int conversation(int count, const pam_message **messages, pam_response **output, void *data) {
    auto password = static_cast<std::string *>(data);
    auto responses = static_cast<pam_response *>(std::calloc(count, sizeof(pam_response)));
    if (!responses) return PAM_BUF_ERR;
    for (int index = 0; index < count; ++index) {
        if (messages[index]->msg_style == PAM_PROMPT_ECHO_OFF) {
            responses[index].resp = ::strdup(password->c_str());
            if (!responses[index].resp) {
                for (int before = 0; before < index; ++before) std::free(responses[before].resp);
                std::free(responses);
                return PAM_BUF_ERR;
            }
        } else if (messages[index]->msg_style != PAM_TEXT_INFO
                   && messages[index]->msg_style != PAM_ERROR_MSG) {
            for (int before = 0; before < index; ++before) std::free(responses[before].resp);
            std::free(responses);
            return PAM_CONV_ERR;
        }
    }
    *output = responses;
    return PAM_SUCCESS;
}
}

int main(int count, char **arguments) {
    if (::getuid() != 0 || ::geteuid() != 0 || count != 3) return 80;
    const auto account = ::getpwnam(arguments[1]);
    if (!account || account->pw_uid < 1000 || account->pw_uid >= 17384) return 81;
    const auto uid = account->pw_uid, gid = account->pw_gid;
    struct stat originalNetwork{}, originalMounts{};
    if (::stat("/proc/self/ns/net", &originalNetwork) != 0
            || ::stat("/proc/self/ns/mnt", &originalMounts) != 0) return 90;
    std::string password;
    if (!std::getline(std::cin, password) || password.empty() || password.size() > 1024) return 82;
    pam_conv callback{conversation, &password};
    pam_handle_t *handle = nullptr;
    int status = pam_start("karyo-network-qa", arguments[1], &callback, &handle);
    if (status != PAM_SUCCESS) return 83;
    auto check = [&](int value, const char *phase) {
        if (value == PAM_SUCCESS) return true;
        std::cerr << phase << ": " << pam_strerror(handle, value) << '\n';
        return false;
    };
    if (!check(pam_authenticate(handle, 0), "authenticate")
            || !check(pam_acct_mgmt(handle, 0), "account")) {
        pam_end(handle, PAM_AUTH_ERR);
        return 84;
    }
    std::fill(password.begin(), password.end(), '\0');
    pam_set_item(handle, PAM_TTY, "/dev/tty8");
    pam_putenv(handle, "XDG_SESSION_TYPE=wayland");
    pam_putenv(handle, "XDG_SESSION_DESKTOP=karyo");
    pam_putenv(handle, "XDG_SESSION_CLASS=user");
    pam_putenv(handle, "XDG_SEAT=seat0");
    pam_putenv(handle, "XDG_VTNR=8");
    if (::initgroups(arguments[1], gid) != 0 || !check(pam_setcred(handle, PAM_ESTABLISH_CRED), "credentials")
            || !check(pam_open_session(handle, 0), "open")) {
        pam_close_session(handle, 0);
        pam_end(handle, PAM_SESSION_ERR);
        return 85;
    }
    const auto child = ::fork();
    int result = 86;
    if (child == 0) {
        if (::setgid(gid) != 0 || ::setuid(uid) != 0) _exit(87);
        char *command[] = {const_cast<char *>("python3"), const_cast<char *>("-I"),
                           const_cast<char *>("-c"), arguments[2], nullptr};
        ::execve("/usr/bin/python3", command, pam_getenvlist(handle));
        _exit(88);
    }
    if (child > 0) {
        int exitStatus = 0;
        if (::waitpid(child, &exitStatus, 0) == child && WIFEXITED(exitStatus))
            result = WEXITSTATUS(exitStatus);
    }
    if (!check(pam_close_session(handle, 0), "close")) result = 89;
    pam_setcred(handle, PAM_DELETE_CRED);
    pam_end(handle, PAM_SUCCESS);
    struct stat currentNetwork{}, currentMounts{};
    if (::stat("/proc/self/ns/net", &currentNetwork) != 0 || ::stat("/proc/self/ns/mnt", &currentMounts) != 0
            || originalNetwork.st_dev != currentNetwork.st_dev || originalNetwork.st_ino != currentNetwork.st_ino
            || originalMounts.st_dev != currentMounts.st_dev || originalMounts.st_ino != currentMounts.st_ino)
        return 91;
    std::cout << "{\"privileged_parent_context_restored\":true}\n";
    return result;
}

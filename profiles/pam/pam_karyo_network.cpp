// SPDX-License-Identifier: GPL-3.0-or-later
// A session-only namespace handoff. It neither authenticates nor starts a VPN.
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <linux/nsfs.h>
#include <sched.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
constexpr const char *Address = "/run/karyo/profiles.sock";
constexpr const char *DataKey = "karyo-network-entry-v1";
constexpr std::uint32_t Magic = 0x4b4e4554;

class Descriptor {
public:
    explicit Descriptor(int value = -1) : value_(value) {}
    ~Descriptor() { if (value_ >= 0) ::close(value_); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
    Descriptor(Descriptor &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
    Descriptor &operator=(Descriptor &&other) noexcept {
        if (this != &other) {
            if (value_ >= 0) ::close(value_);
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }
    int get() const { return value_; }
private:
    int value_;
};

void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

void syscallResult(bool value, const char *message) {
    if (!value) {
        const int code = errno;
        throw std::runtime_error(std::string(message) + ": " + std::strerror(code));
    }
}

Descriptor openNamespace(const char *kind) {
    const auto path = std::string("/proc/self/ns/") + kind;
    Descriptor fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    require(fd.get() >= 0, "cannot capture the original namespace");
    return fd;
}

bool identifier(const std::string &value) {
    if (value.empty() || value.size() > 32) return false;
    for (unsigned char character : value)
        if (!std::isalnum(character) && character != '_' && character != '-'
                && character != '.' && character != '$') return false;
    return true;
}

struct Reply {
    std::uint32_t status = 1;
    std::size_t count = 0;
    std::array<Descriptor, 3> descriptors;
};

struct stat socketIdentity() {
    struct stat parent{}, info{};
    require(::lstat("/run/karyo", &parent) == 0 && S_ISDIR(parent.st_mode)
            && parent.st_uid == 0 && !(parent.st_mode & 0022), "unsafe controller directory");
    require(::lstat(Address, &info) == 0 && S_ISSOCK(info.st_mode)
            && info.st_uid == 0 && info.st_gid == 0 && info.st_nlink == 1
            && (info.st_mode & 0777) == 0600, "unsafe controller socket");
    return info;
}

void writeAll(int fd, const void *data, std::size_t size) {
    auto bytes = static_cast<const char *>(data);
    while (size) {
        const auto written = ::send(fd, bytes, size, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR) continue;
        require(written > 0, "controller request failed");
        bytes += written;
        size -= static_cast<std::size_t>(written);
    }
}

Reply request(const std::string &operation, const std::string &user, const std::string &service) {
    require(identifier(user) && identifier(service), "invalid PAM account or service");
    const auto before = socketIdentity();
    Descriptor socket(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    require(socket.get() >= 0, "cannot open controller connection");
    const timeval timeout{60, 0};
    require(::setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0
            && ::setsockopt(socket.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
            "cannot bound controller connection");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, Address);
    require(::connect(socket.get(), reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
            "controller unavailable");
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    require(::getsockopt(socket.get(), SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0
            && length == sizeof(credentials) && credentials.pid > 0
            && credentials.uid == 0 && credentials.gid == 0, "controller owner changed");
    const auto after = socketIdentity();
    require(before.st_dev == after.st_dev && before.st_ino == after.st_ino,
            "controller socket changed");
    const auto body = std::string("{\"version\":1,\"operation\":\"login-") + operation
        + "\",\"user\":\"" + user + "\",\"service\":\"" + service + "\"}";
    const auto size = htonl(static_cast<std::uint32_t>(body.size()));
    writeAll(socket.get(), &size, sizeof(size));
    writeAll(socket.get(), body.data(), body.size());
    require(::shutdown(socket.get(), SHUT_WR) == 0, "cannot finish controller request");

    std::array<std::uint32_t, 3> header{};
    alignas(cmsghdr) char control[CMSG_SPACE(3 * sizeof(int))]{};
    iovec data{header.data(), sizeof(header)};
    msghdr message{};
    message.msg_iov = &data; message.msg_iovlen = 1;
    message.msg_control = control; message.msg_controllen = sizeof(control);
    ssize_t received;
    do { received = ::recvmsg(socket.get(), &message, MSG_CMSG_CLOEXEC); }
    while (received < 0 && errno == EINTR);
    require(received > 0, "missing controller response");
    Reply reply;
    // Adopt all delivered descriptors before checking the message so errors
    // close them too. No namespace path is accepted from a response.
    for (auto item = CMSG_FIRSTHDR(&message); item; item = CMSG_NXTHDR(&message, item)) {
        require(item->cmsg_level == SOL_SOCKET && item->cmsg_type == SCM_RIGHTS
                && item->cmsg_len >= CMSG_LEN(0), "unexpected controller ancillary data");
        const auto bytes = item->cmsg_len - CMSG_LEN(0);
        require(bytes % sizeof(int) == 0, "invalid controller descriptors");
        const auto values = reinterpret_cast<const int *>(CMSG_DATA(item));
        for (std::size_t index = 0; index < bytes / sizeof(int); ++index) {
            Descriptor fd(values[index]);
            require(reply.count < reply.descriptors.size(), "too many controller descriptors");
            reply.descriptors[reply.count++] = std::move(fd);
        }
    }
    require(!(message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)), "truncated controller response");
    std::size_t offset = static_cast<std::size_t>(received);
    while (offset < sizeof(header)) {
        const auto next = ::recv(socket.get(), reinterpret_cast<char *>(header.data()) + offset,
                                 sizeof(header) - offset, 0);
        if (next < 0 && errno == EINTR) continue;
        require(next > 0, "incomplete controller response");
        offset += static_cast<std::size_t>(next);
    }
    require(ntohl(header[0]) == Magic && ntohl(header[2]) == reply.count,
            "invalid controller response header");
    reply.status = ntohl(header[1]);
    require(reply.status <= 2 && (reply.status == 0 || reply.count == 0),
            "invalid controller response status");
    return reply;
}

void bindReadOnly(const std::string &source, const char *target) {
    syscallResult(::mount(source.c_str(), target, nullptr, MS_BIND, nullptr) == 0,
            "cannot attach the private service view");
    syscallResult(::mount(nullptr, target, nullptr, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV,
                    nullptr) == 0, "cannot protect the private service view");
}

void maskSharedSocket(const char *path) {
    struct stat info{};
    if (::lstat(path, &info) < 0 && errno == ENOENT) return;
    require(S_ISSOCK(info.st_mode), "unexpected shared resolver endpoint");
    bindReadOnly("/dev/null", path);
}

struct Entry {
    std::string user, service;
    Descriptor originalNetwork{openNamespace("net")};
    Descriptor originalMounts{openNamespace("mnt")};
    Descriptor hostBusMount;
    bool mountsChanged = false;
    bool networkChanged = false;
    bool closed = false;
    bool ignored = false;

    void attachHostBus() {
        if (hostBusMount.get() < 0) return;
        // pam_systemd has now registered the login using the root host bus.
        // Only the final user session receives the ordinary UID's proxy.
        syscallResult(::move_mount(hostBusMount.get(), "", AT_FDCWD,
            "/run/dbus/system_bus_socket", MOVE_MOUNT_F_EMPTY_PATH) == 0,
            "cannot attach the profile host-service bus");
        syscallResult(::mount(nullptr, "/run/dbus/system_bus_socket", nullptr,
            MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, nullptr) == 0,
            "cannot protect the profile host-service bus");
        hostBusMount = Descriptor();
    }

    bool restore() noexcept {
        bool result = true;
        if (networkChanged) {
            if (::setns(originalNetwork.get(), CLONE_NEWNET) == 0) networkChanged = false;
            else result = false;
        }
        if (mountsChanged) {
            if (::setns(originalMounts.get(), CLONE_NEWNS) == 0) mountsChanged = false;
            else result = false;
        }
        return result;
    }

    bool close() noexcept {
        bool result = true;
        if (!closed) {
            try {
                auto reply = request("close", user, service);
                result = reply.status == 0 && reply.count == 0;
            } catch (...) { result = false; }
            closed = true;
        }
        return restore() && result;
    }

    ~Entry() { close(); }
};

void cleanup(pam_handle_t *handle, void *data, int) {
    auto entry = static_cast<Entry *>(data);
    if (!entry->close()) pam_syslog(handle, LOG_ERR, "main network entry cleanup requires review");
    delete entry;
}

Entry *entryData(pam_handle_t *handle) {
    const void *value = nullptr;
    const int result = pam_get_data(handle, DataKey, &value);
    require(result == PAM_SUCCESS || result == PAM_NO_MODULE_DATA, "cannot read PAM entry state");
    return result == PAM_SUCCESS ? const_cast<Entry *>(static_cast<const Entry *>(value)) : nullptr;
}

std::pair<std::string, std::string> owner(pam_handle_t *handle) {
    const void *user = nullptr, *service = nullptr;
    require(pam_get_item(handle, PAM_USER, &user) == PAM_SUCCESS && user
            && pam_get_item(handle, PAM_SERVICE, &service) == PAM_SUCCESS && service,
            "PAM account and service are required");
    return {static_cast<const char *>(user), static_cast<const char *>(service)};
}

int ignoreEntry(pam_handle_t *handle, const std::pair<std::string, std::string> &identity) {
    auto entry = std::make_unique<Entry>();
    entry->user = identity.first; entry->service = identity.second;
    entry->ignored = true; entry->closed = true;
    require(pam_set_data(handle, DataKey, entry.get(), cleanup) == PAM_SUCCESS,
            "cannot retain the unmanaged PAM entry");
    entry.release();
    return PAM_IGNORE;
}

std::string phase(int count, const char **args) {
    require(::getuid() == 0 && ::geteuid() == 0 && (count == 1 || count == 2) && args && args[0],
            "root PAM session entry requires a fixed phase");
    if (count == 2)
        require(args[1] && std::string(args[1]) == "desktop=karyo", "unsupported PAM desktop selector");
    const std::string value(args[0]);
    require(value == "prepare" || value == "attach", "unsupported PAM network phase");
    return value;
}

int prepare(pam_handle_t *handle, bool karyoOnly) {
    require(!entryData(handle), "PAM network entry is already prepared");
    const auto identity = owner(handle);
    const char *desktop = pam_getenv(handle, "XDG_SESSION_DESKTOP");
    // LightDM supplies the selected desktop before pam_open_session. Other
    // installed desktops retain their ordinary login environment. This selector
    // grants no network authority: the root host still captures the PAM peer.
    if (karyoOnly && (!desktop || std::string(desktop) != "karyo")) return ignoreEntry(handle, identity);
    auto reply = request("prepare", identity.first, identity.second);
    if (reply.status == 2) return ignoreEntry(handle, identity);
    require(reply.status == 0 && (reply.count == 2 || reply.count == 3), "main network preparation failed");
    struct stat resolver{};
    require(::ioctl(reply.descriptors[0].get(), NS_GET_NSTYPE) == CLONE_NEWNET
            && ::fstat(reply.descriptors[1].get(), &resolver) == 0 && S_ISDIR(resolver.st_mode),
            "invalid main network context");
    // The received directory FD belongs to the controller's mount namespace.
    // Clone a detached mount rather than resolving /proc/self/fd across the
    // subsequent unshare and trying to bind a foreign namespace's mount.
    Descriptor resolverMount(::open_tree(reply.descriptors[1].get(), "",
        AT_EMPTY_PATH | OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC));
    syscallResult(resolverMount.get() >= 0, "cannot clone the resolver mount");
    auto entry = std::make_unique<Entry>();
    entry->user = identity.first; entry->service = identity.second;
    if (reply.count == 3) {
        struct stat socket{}, target{};
        require(::fstat(reply.descriptors[2].get(), &socket) == 0 && S_ISSOCK(socket.st_mode)
                && ::lstat("/run/dbus/system_bus_socket", &target) == 0
                && S_ISSOCK(target.st_mode) && target.st_uid == 0,
                "invalid main host-service socket context");
        entry->hostBusMount = Descriptor(::open_tree(reply.descriptors[2].get(), "",
            AT_EMPTY_PATH | OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC));
        syscallResult(entry->hostBusMount.get() >= 0, "cannot clone the host-service socket mount");
    }
    syscallResult(::unshare(CLONE_NEWNS) == 0, "cannot prepare the session mount namespace");
    entry->mountsChanged = true;
    // Accept later host mounts (notably /run/user/$UID, created by
    // pam_systemd), while preventing session mounts from propagating outward.
    syscallResult(::mount(nullptr, "/", nullptr, MS_REC | MS_SLAVE, nullptr) == 0,
            "cannot isolate session mount propagation");
    syscallResult(::move_mount(resolverMount.get(), "", AT_FDCWD, "/run/systemd/resolve",
                              MOVE_MOUNT_F_EMPTY_PATH) == 0, "cannot attach the resolver mount");
    syscallResult(::mount(nullptr, "/run/systemd/resolve", nullptr,
        MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, nullptr) == 0,
        "cannot protect the resolver mount");
    maskSharedSocket("/run/nscd/socket");
    maskSharedSocket("/run/avahi-daemon/socket");
    syscallResult(::setns(reply.descriptors[0].get(), CLONE_NEWNET) == 0,
            "cannot enter the prepared main network");
    entry->networkChanged = true;
    require(pam_set_data(handle, DataKey, entry.get(), cleanup) == PAM_SUCCESS,
            "cannot retain the PAM network entry");
    entry.release();
    return PAM_SUCCESS;
}
} // namespace

extern "C" {
PAM_EXTERN int pam_sm_open_session(pam_handle_t *handle, int, int count, const char **args) {
    try {
        const auto selected = phase(count, args);
        if (selected == "prepare") return prepare(handle, count == 2);
        auto entry = entryData(handle);
        require(entry, "PAM network prepare phase is missing");
        const auto identity = owner(handle);
        require(identity.first == entry->user && identity.second == entry->service,
                "PAM account changed during network preparation");
        if (entry->ignored) return PAM_IGNORE;
        entry->attachHostBus();
        auto reply = request("attach", entry->user, entry->service);
        require(reply.status == 0 && reply.count == 0, "main login attachment failed");
        return PAM_SUCCESS;
    } catch (const std::exception &error) {
        pam_syslog(handle, LOG_ERR, "network session entry: %s", error.what());
        return PAM_SESSION_ERR;
    } catch (...) {
        return PAM_SESSION_ERR;
    }
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t *handle, int, int count, const char **args) {
    try {
        const auto selected = phase(count, args);
        auto entry = entryData(handle);
        if (!entry || entry->ignored || selected == "attach") return PAM_IGNORE;
        require(entry->close(), "main network close requires review");
        return PAM_SUCCESS;
    } catch (const std::exception &error) {
        pam_syslog(handle, LOG_ERR, "network session close: %s", error.what());
        return PAM_SESSION_ERR;
    } catch (...) {
        return PAM_SESSION_ERR;
    }
}
}

// File: tests/fake_compositor/socket_io.hpp
// Purpose: The only place sockets, and the passing of file descriptors with SCM_RIGHTS, meet untyped memory. sendmsg,
//          recvmsg, iovec and CMSG all work through untyped pointers, and no typed call passes a descriptor. It
//          is for the test compositor only, and it AWAITS THE OWNER'S APPROVAL: see docs/Maturity.md.
//          Everything callers see is typed. tests/check_source_rules.sh keeps it the only such place.
#pragma once

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "jCppGUI/v001/error.hpp"
#include "untrusted.hpp"

namespace jgui_test::wire {

using jgui::detail::UniqueFd;

// A listening socket at that path. Refuses a path that does not fit a socket address.
[[nodiscard]] inline std::expected<UniqueFd, jgui::Error> listen_at(std::string_view path) {
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path)) return jgui::make_error(jgui::ErrorCode::SizeLimit, "the socket path is too long");
    address.sun_family = AF_UNIX;
    std::ranges::copy(path, std::span{address.sun_path}.begin());
    UniqueFd socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (socket.get() < 0) return jgui::make_error(jgui::ErrorCode::System, "socket failed");
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(socket.get(), 1) != 0)
        return jgui::make_error(jgui::ErrorCode::System, "bind or listen failed");
    return socket;
}

[[nodiscard]] inline std::expected<UniqueFd, jgui::Error> accept_one(int listener) {
    UniqueFd client{::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC)};
    if (client.get() < 0) return jgui::make_error(jgui::ErrorCode::System, "accept failed");
    return client;
}

// Two connected sockets, for a client that takes its connection through WAYLAND_SOCKET.
struct SocketPair {
    UniqueFd ours;
    UniqueFd theirs;
};

[[nodiscard]] inline std::expected<SocketPair, jgui::Error> socket_pair() {
    std::array<int, 2> ends{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends.data()) != 0) return jgui::make_error(jgui::ErrorCode::System, "socketpair failed");
    return SocketPair{UniqueFd{ends[0]}, UniqueFd{ends[1]}};
}

// Sends what it can without waiting, with at most one descriptor attached to the first byte. Answers how many
// bytes went: zero when the socket is full. A closed peer is an Error, with no signal raised for it.
[[nodiscard]] inline std::expected<std::size_t, jgui::Error> send_some(int socket, std::span<const std::byte> bytes, int attached_fd = -1) {
    iovec part{};
    part.iov_base = const_cast<std::byte*>(bytes.data());
    part.iov_len = bytes.size();
    msghdr message{};
    message.msg_iov = &part;
    message.msg_iovlen = 1;
    std::array<std::byte, CMSG_SPACE(sizeof(int))> control{};
    if (attached_fd >= 0) {
        message.msg_control = control.data();
        message.msg_controllen = control.size();
        cmsghdr* header = CMSG_FIRSTHDR(&message);
        if (header == nullptr) return jgui::make_error(jgui::ErrorCode::System, "no room for the descriptor's header");
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        const auto fd_bytes = std::bit_cast<std::array<unsigned char, sizeof(int)>>(attached_fd);
        std::ranges::copy(fd_bytes, CMSG_DATA(header));
    }
    const ::ssize_t sent = ::sendmsg(socket, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EINTR) return std::size_t{0};
        return jgui::make_error(jgui::ErrorCode::System, "sendmsg failed");
    }
    return static_cast<std::size_t>(sent);
}

// What a read gave: how many bytes, and whether the peer has closed. Descriptors go to the end of the list.
struct Received {
    std::size_t bytes{};
    bool closed{};
};

[[nodiscard]] inline std::expected<Received, jgui::Error> receive_some(int socket, std::span<std::byte> into, std::vector<UniqueFd>& descriptors) {
    iovec part{};
    part.iov_base = into.data();
    part.iov_len = into.size();
    std::array<std::byte, CMSG_SPACE(sizeof(int) * 28)> control{};
    msghdr message{};
    message.msg_iov = &part;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    const ::ssize_t got = ::recvmsg(socket, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (got < 0) {
        if (errno == EAGAIN || errno == EINTR) return Received{};
        if (errno == ECONNRESET) return Received{0, true};
        return jgui::make_error(jgui::ErrorCode::System, "recvmsg failed");
    }
    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        const std::size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        const unsigned char* next = CMSG_DATA(header);
        for (std::size_t i = 0; i < count; ++i) {
            std::array<unsigned char, sizeof(int)> fd_bytes{};
            const auto copied = std::ranges::copy_n(next, sizeof(int), fd_bytes.begin());
            next = copied.in;
            descriptors.emplace_back(std::bit_cast<int>(fd_bytes));
        }
    }
    return Received{static_cast<std::size_t>(got), got == 0};
}

}  // namespace jgui_test::wire

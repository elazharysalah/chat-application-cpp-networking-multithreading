#include "net.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

namespace net {

namespace {

constexpr std::size_t kMaxChunk = 1 << 20;

std::string wideToUtf8(const wchar_t* text, int length) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), size, nullptr, nullptr);
    return out;
}

}  // namespace

WinsockSession::WinsockSession() {
    WSADATA data{};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

WinsockSession::~WinsockSession() {
    if (ok_)
        WSACleanup();
}

void Socket::close() {
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

void Socket::shutdownBoth() const {
    if (s_ != INVALID_SOCKET)
        ::shutdown(s_, SD_BOTH);
}

std::string errorMessage(int code) {
    switch (code) {
    case WSAECONNREFUSED: return "Connection refused. Is the server running?";
    case WSAETIMEDOUT: return "The connection timed out.";
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA: return "Host not found.";
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH: return "The network is unreachable.";
    case WSAECONNRESET: return "The connection was reset.";
    case WSAECONNABORTED: return "The connection was aborted.";
    case WSAEADDRINUSE: return "That port is already in use (is another server running?).";
    case WSAEACCES: return "Access to the port was denied.";
    default: break;
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(code), 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message = length ? wideToUtf8(buffer, static_cast<int>(length)) : "Unknown error";
    if (buffer)
        LocalFree(buffer);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
        message.pop_back();
    return message + " (code " + std::to_string(code) + ")";
}

bool sendAll(SOCKET s, const void* data, std::size_t length) {
    const char* p = static_cast<const char*>(data);
    while (length > 0) {
        const int n = ::send(s, p, static_cast<int>(std::min(length, kMaxChunk)), 0);
        if (n <= 0)
            return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return true;
}

bool recvAll(SOCKET s, void* data, std::size_t length) {
    char* p = static_cast<char*>(data);
    while (length > 0) {
        // TCP is a byte stream: one recv() may return any part of a frame.
        const int n = ::recv(s, p, static_cast<int>(std::min(length, kMaxChunk)), 0);
        if (n <= 0)  // 0 = orderly shutdown by the peer, < 0 = error
            return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return true;
}

bool sendFrame(SOCKET s, const std::vector<uint8_t>& payload) {
    if (payload.size() > 0xFFFFFFFFu)
        return false;
    const auto n = static_cast<uint32_t>(payload.size());
    // Header and payload go out in a single send() so they share a TCP segment.
    std::vector<uint8_t> frame(4 + payload.size());
    frame[0] = static_cast<uint8_t>(n >> 24);
    frame[1] = static_cast<uint8_t>(n >> 16);
    frame[2] = static_cast<uint8_t>(n >> 8);
    frame[3] = static_cast<uint8_t>(n);
    std::copy(payload.begin(), payload.end(), frame.begin() + 4);
    return sendAll(s, frame.data(), frame.size());
}

bool recvFrame(SOCKET s, std::vector<uint8_t>& payload, uint32_t maxSize) {
    uint8_t header[4];
    if (!recvAll(s, header, sizeof header))
        return false;
    const uint32_t n = (uint32_t{header[0]} << 24) | (uint32_t{header[1]} << 16) |
                       (uint32_t{header[2]} << 8) | uint32_t{header[3]};
    if (n > maxSize)
        return false;
    payload.resize(n);
    return n == 0 || recvAll(s, payload.data(), n);
}

Socket listenTcp(uint16_t port, uint16_t& boundPort, std::string& error) {
    // Prefer a dual-stack IPv6 socket so IPv4 and IPv6 clients can both connect.
    Socket s(::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP));
    const bool ipv6 = s.valid();
    if (ipv6) {
        DWORD off = 0;
        setsockopt(s.get(), IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof off);
    } else {
        s = Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!s.valid()) {
            error = errorMessage(WSAGetLastError());
            return {};
        }
    }

    // Stop other processes from binding the same port while we own it.
    BOOL exclusive = TRUE;
    setsockopt(s.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
               sizeof exclusive);

    int rc = 0;
    if (ipv6) {
        sockaddr_in6 addr{};  // zero-initialised address == in6addr_any
        addr.sin6_family = AF_INET6;
        addr.sin6_port = htons(port);
        rc = ::bind(s.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        rc = ::bind(s.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    }
    if (rc == SOCKET_ERROR || ::listen(s.get(), SOMAXCONN) == SOCKET_ERROR) {
        error = errorMessage(WSAGetLastError());
        return {};
    }

    sockaddr_storage local{};
    int length = sizeof local;
    boundPort = port;
    if (getsockname(s.get(), reinterpret_cast<sockaddr*>(&local), &length) == 0) {
        boundPort = local.ss_family == AF_INET6
                        ? ntohs(reinterpret_cast<const sockaddr_in6*>(&local)->sin6_port)
                        : ntohs(reinterpret_cast<const sockaddr_in*>(&local)->sin_port);
    }
    return s;
}

Socket connectTcp(const std::string& host, uint16_t port, int timeoutMs,
                  const std::function<bool()>& cancelled, std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* results = nullptr;
    const std::string service = std::to_string(port);
    if (const int rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &results); rc != 0) {
        error = errorMessage(rc);
        return {};
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> guard(results, &freeaddrinfo);

    int lastError = WSAECONNREFUSED;
    for (const addrinfo* ai = results; ai; ai = ai->ai_next) {
        Socket s(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!s.valid()) {
            lastError = WSAGetLastError();
            continue;
        }

        // Non-blocking connect + select() lets us honour a timeout and a
        // cancel request instead of blocking for the OS default (~20 s).
        u_long nonBlocking = 1;
        ioctlsocket(s.get(), FIONBIO, &nonBlocking);

        if (::connect(s.get(), ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == SOCKET_ERROR) {
            const int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK) {
                lastError = e;
                continue;
            }

            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            bool connected = false;
            for (;;) {
                if (cancelled && cancelled()) {
                    error = "Cancelled.";
                    return {};
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    lastError = WSAETIMEDOUT;
                    break;
                }
                fd_set writable, failed;
                FD_ZERO(&writable);
                FD_ZERO(&failed);
                FD_SET(s.get(), &writable);
                FD_SET(s.get(), &failed);
                timeval tick{0, 100 * 1000};
                const int ready = ::select(0, nullptr, &writable, &failed, &tick);
                if (ready == SOCKET_ERROR) {
                    lastError = WSAGetLastError();
                    break;
                }
                if (ready == 0)
                    continue;
                if (FD_ISSET(s.get(), &failed)) {
                    int soError = 0;
                    int len = sizeof soError;
                    getsockopt(s.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &len);
                    lastError = soError ? soError : WSAECONNREFUSED;
                    break;
                }
                connected = true;
                break;
            }
            if (!connected)
                continue;
        }

        nonBlocking = 0;
        ioctlsocket(s.get(), FIONBIO, &nonBlocking);
        setNoDelay(s.get());
        return s;
    }

    error = errorMessage(lastError);
    return {};
}

void setNoDelay(SOCKET s) {
    // Chat messages are small and latency-sensitive: disable Nagle's algorithm.
    BOOL on = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
}

void setRecvTimeout(SOCKET s, unsigned long ms) {
    DWORD value = ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof value);
}

void setSendTimeout(SOCKET s, unsigned long ms) {
    DWORD value = ms;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&value), sizeof value);
}

std::string peerAddress(SOCKET s) {
    sockaddr_storage addr{};
    int length = sizeof addr;
    if (getpeername(s, reinterpret_cast<sockaddr*>(&addr), &length) != 0)
        return "?";
    char host[NI_MAXHOST] = {};
    char service[NI_MAXSERV] = {};
    if (getnameinfo(reinterpret_cast<const sockaddr*>(&addr), length, host, sizeof host, service,
                    sizeof service, NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        return "?";

    std::string h = host;
    if (h.rfind("::ffff:", 0) == 0)  // IPv4 client on a dual-stack socket
        h = h.substr(7);
    if (h.find(':') != std::string::npos)
        return "[" + h + "]:" + service;
    return h + ":" + service;
}

std::vector<std::string> localIPv4Addresses() {
    std::vector<std::string> out;
    char name[256] = {};
    if (gethostname(name, sizeof name) != 0)
        return out;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (getaddrinfo(name, nullptr, &hints, &results) != 0)
        return out;
    for (const addrinfo* ai = results; ai; ai = ai->ai_next) {
        char text[INET_ADDRSTRLEN] = {};
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(ai->ai_addr);
        if (inet_ntop(AF_INET, &v4->sin_addr, text, sizeof text))
            if (std::find(out.begin(), out.end(), text) == out.end())
                out.emplace_back(text);
    }
    freeaddrinfo(results);
    return out;
}

}  // namespace net

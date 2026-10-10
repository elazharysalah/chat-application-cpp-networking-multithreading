#pragma once

// Thin RAII layer over Winsock2: socket ownership, framed send/receive and
// connect/listen helpers. Everything is blocking except connectTcp(), which
// polls so that a pending connection attempt can be cancelled.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace net {

// WSAStartup/WSACleanup for the lifetime of the object.
class WinsockSession {
public:
    WinsockSession();
    ~WinsockSession();
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

// Move-only owner of a SOCKET handle.
class Socket {
public:
    Socket() = default;
    explicit Socket(SOCKET s) : s_(s) {}
    ~Socket() { close(); }

    Socket(Socket&& other) noexcept : s_(std::exchange(other.s_, INVALID_SOCKET)) {}
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            s_ = std::exchange(other.s_, INVALID_SOCKET);
        }
        return *this;
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    SOCKET get() const { return s_; }
    bool valid() const { return s_ != INVALID_SOCKET; }
    SOCKET release() { return std::exchange(s_, INVALID_SOCKET); }
    void close();

    // Safe to call from another thread: makes blocked recv()/send() calls on
    // this socket return immediately, which is how we stop I/O threads.
    void shutdownBoth() const;

private:
    SOCKET s_ = INVALID_SOCKET;
};

// Human-readable description of a Winsock error code (UTF-8).
std::string errorMessage(int code);

bool sendAll(SOCKET s, const void* data, std::size_t length);
bool recvAll(SOCKET s, void* data, std::size_t length);

// Frame = [u32 big-endian length][payload]. recvFrame() fails on disconnect,
// socket errors, or frames larger than maxSize.
bool sendFrame(SOCKET s, const std::vector<uint8_t>& payload);
bool recvFrame(SOCKET s, std::vector<uint8_t>& payload, uint32_t maxSize);

// Listens on all interfaces (dual-stack IPv6 + IPv4 when available).
// Pass port 0 to let the OS pick; the chosen port is written to boundPort.
Socket listenTcp(uint16_t port, uint16_t& boundPort, std::string& error);

// Tries every address `host` resolves to. `cancelled` is polled every 100 ms.
Socket connectTcp(const std::string& host, uint16_t port, int timeoutMs,
                  const std::function<bool()>& cancelled, std::string& error);

void setNoDelay(SOCKET s);
void setRecvTimeout(SOCKET s, unsigned long ms);
void setSendTimeout(SOCKET s, unsigned long ms);

std::string peerAddress(SOCKET s);
std::vector<std::string> localIPv4Addresses();

}  // namespace net

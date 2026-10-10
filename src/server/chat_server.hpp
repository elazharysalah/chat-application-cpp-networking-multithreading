#pragma once

#include "blocking_queue.hpp"
#include "net.hpp"
#include "protocol.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace chat {

// Threading model
// ---------------
//  * accept thread  - whoever calls run(); accepts sockets and reaps finished sessions
//  * reader thread  - one per client; handshake, then parses and handles requests
//  * writer thread  - one per client; drains that client's outbox onto the socket
//
// Sending to a client only enqueues an encoded frame, so broadcasting never
// blocks on a slow peer, and no socket I/O happens while mutex_ is held.
class ChatServer {
public:
    explicit ChatServer(uint16_t port, std::string name = "elazharyLearn (ENSEM University)");
    ~ChatServer();
    ChatServer(const ChatServer&) = delete;
    ChatServer& operator=(const ChatServer&) = delete;

    // Runs the accept loop until stop() is called. Returns false if the port
    // could not be opened.
    bool run();

    // Thread-safe; may be called from any thread (e.g. a Ctrl+C handler).
    void stop();

    bool isListening() const { return listening_; }
    uint16_t port() const { return boundPort_; }  // actual port once listening (useful with port 0)

private:
    using Frame = std::shared_ptr<const std::vector<uint8_t>>;  // encoded once, shared by every recipient

    struct Session {
        uint64_t id = 0;
        net::Socket socket;
        std::string address;
        std::string name;  // empty until the handshake succeeds; written under mutex_
        BlockingQueue<Frame> outbox;
        std::thread reader;
        std::thread writer;
        std::atomic<bool> finished{false};
    };
    using SessionPtr = std::shared_ptr<Session>;

    void startSession(net::Socket socket);
    void readerLoop(SessionPtr session);
    static void writerLoop(SessionPtr session);

    bool handshake(Session& s);
    void handle(Session& s, const Message& m);
    void leave(Session& s);

    void send(Session& s, const Message& m);
    void enqueue(Session& s, const Frame& frame);
    void broadcastLocked(const Message& m, const Session* except = nullptr);
    Session* findByNameLocked(const std::string& name);
    std::vector<std::string> onlineUsersLocked() const;

    void reapFinished();
    void shutdownAllSessions();
    void closeListener();

    const uint16_t requestedPort_;
    const std::string serverName_;
    std::atomic<uint16_t> boundPort_{0};
    std::atomic<bool> listening_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<SOCKET> listenSocket_{INVALID_SOCKET};
    std::atomic<uint64_t> nextId_{1};

    mutable std::mutex mutex_;  // guards sessions_, history_ and Session::name
    std::list<SessionPtr> sessions_;
    std::deque<Message> history_;
};

}  // namespace chat

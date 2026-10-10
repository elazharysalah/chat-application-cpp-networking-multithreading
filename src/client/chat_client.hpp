#pragma once

#include "blocking_queue.hpp"
#include "net.hpp"
#include "protocol.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace chat {

struct ClientEvent {
    enum class Kind { ConnectFailed, Disconnected, Message };

    Kind kind = Kind::Message;
    Message message;    // Kind::Message
    std::string error;  // Kind::ConnectFailed / Kind::Disconnected
};

// Networking half of the client, independent of any UI.
//
// connect() starts a network thread that connects, sends Hello and then reads
// frames; it spawns a writer thread that drains the outbox. Everything the
// server says is turned into ClientEvents, which the UI drains each frame with
// pollEvents() - the UI thread never touches a socket and never blocks.
//
// connect(), disconnect() and send*() are meant to be called from one thread.
class ChatClient {
public:
    ChatClient() = default;
    ~ChatClient();
    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    void connect(const std::string& host, uint16_t port, const std::string& username);
    // Stops all network threads and discards pending events. Safe to call at any time.
    void disconnect();

    bool sendChat(const std::string& text);
    bool sendPrivate(const std::string& to, const std::string& text);
    bool sendTyping(const std::string& to, bool typing);

    std::vector<ClientEvent> pollEvents();
    std::optional<ClientEvent> waitEvent(std::chrono::milliseconds timeout);

private:
    using Outbox = BlockingQueue<std::vector<uint8_t>>;

    void networkLoop(std::string host, uint16_t port, std::string username, std::shared_ptr<Outbox> outbox);
    bool enqueue(const Message& message);

    std::thread thread_;
    std::shared_ptr<Outbox> outbox_;
    std::atomic<bool> stopRequested_{false};

    std::mutex socketMutex_;
    SOCKET socket_ = INVALID_SOCKET;  // lets disconnect() interrupt a blocking recv()

    BlockingQueue<ClientEvent> events_;
};

}  // namespace chat

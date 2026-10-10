#include "chat_client.hpp"

namespace chat {

namespace {
constexpr int kConnectTimeoutMs = 5000;
}

ChatClient::~ChatClient() { disconnect(); }

void ChatClient::connect(const std::string& host, uint16_t port, const std::string& username) {
    disconnect();
    stopRequested_ = false;
    outbox_ = std::make_shared<Outbox>();
    thread_ = std::thread(&ChatClient::networkLoop, this, host, port, username, outbox_);
}

void ChatClient::disconnect() {
    if (!thread_.joinable())
        return;
    stopRequested_ = true;
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socket_ != INVALID_SOCKET)
            ::shutdown(socket_, SD_BOTH);
    }
    thread_.join();
    outbox_.reset();
    events_.drain();  // whatever the old connection reported is no longer relevant
}

bool ChatClient::sendChat(const std::string& text) {
    Message m;
    m.type = MsgType::SendChat;
    m.text = text;
    return enqueue(m);
}

bool ChatClient::sendPrivate(const std::string& to, const std::string& text) {
    Message m;
    m.type = MsgType::SendPrivate;
    m.to = to;
    m.text = text;
    return enqueue(m);
}

bool ChatClient::sendTyping(const std::string& to, bool typing) {
    Message m;
    m.type = MsgType::Typing;
    m.to = to;
    m.flag = typing;
    return enqueue(m);
}

std::vector<ClientEvent> ChatClient::pollEvents() { return events_.drain(); }

std::optional<ClientEvent> ChatClient::waitEvent(std::chrono::milliseconds timeout) {
    return events_.popFor(timeout);
}

bool ChatClient::enqueue(const Message& message) {
    return outbox_ && outbox_->push(encode(message));
}

void ChatClient::networkLoop(std::string host, uint16_t port, std::string username,
                             std::shared_ptr<Outbox> outbox) {
    // Once the user asked to disconnect, nothing that happens afterwards is news.
    auto report = [this](ClientEvent::Kind kind, std::string error) {
        if (!stopRequested_)
            events_.push(ClientEvent{kind, {}, std::move(error)});
    };

    std::string error;
    net::Socket socket = net::connectTcp(
        host, port, kConnectTimeoutMs, [this] { return stopRequested_.load(); }, error);
    if (!socket.valid()) {
        report(ClientEvent::Kind::ConnectFailed, error);
        return;
    }

    {
        // Publish the socket so disconnect() can shut it down. Checking the
        // flag under the same lock closes the race with a concurrent disconnect().
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (stopRequested_)
            return;
        socket_ = socket.get();
    }

    Message hello;
    hello.type = MsgType::Hello;
    hello.from = username;
    hello.timestamp = nowMs();

    std::thread writer;
    std::string reason = "Lost connection to the server.";
    bool rejected = false;

    if (net::sendFrame(socket.get(), encode(hello))) {
        writer = std::thread([s = socket.get(), outbox] {
            while (std::optional<std::vector<uint8_t>> frame = outbox->pop()) {
                if (!net::sendFrame(s, *frame)) {
                    ::shutdown(s, SD_BOTH);  // wake the reader below
                    break;
                }
            }
        });

        std::vector<uint8_t> frame;
        while (net::recvFrame(socket.get(), frame, kMaxFrameSize)) {
            std::optional<Message> message = decode(frame);
            if (!message) {
                reason = "Received malformed data from the server.";
                break;
            }
            rejected = rejected || message->type == MsgType::Rejected;
            events_.push(ClientEvent{ClientEvent::Kind::Message, std::move(*message), {}});
        }
    } else {
        reason = "Could not complete the handshake with the server.";
    }

    outbox->close();
    socket.shutdownBoth();
    if (writer.joinable())
        writer.join();
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        socket_ = INVALID_SOCKET;
    }
    if (!rejected)  // a rejection already carries its own explanation
        report(ClientEvent::Kind::Disconnected, reason);
}

}  // namespace chat

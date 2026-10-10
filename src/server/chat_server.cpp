#include "chat_server.hpp"

#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace chat {

namespace {

constexpr unsigned long kHandshakeTimeoutMs = 10'000;
constexpr unsigned long kSendTimeoutMs = 5'000;
constexpr std::size_t kHistorySize = 50;
constexpr std::size_t kMaxQueuedFrames = 1024;

char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

bool lessName(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](char x, char y) { return lower(x) < lower(y); });
}

Message notice(MsgType type, std::string text) {
    Message m;
    m.type = type;
    m.text = std::move(text);
    m.timestamp = nowMs();
    return m;
}

std::string plural(std::size_t n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

}  // namespace

ChatServer::ChatServer(uint16_t port, std::string name)
    : requestedPort_(port), serverName_(std::move(name)) {}

ChatServer::~ChatServer() { stop(); }

bool ChatServer::run() {
    std::string error;
    uint16_t bound = 0;
    net::Socket listener = net::listenTcp(requestedPort_, bound, error);
    if (!listener.valid()) {
        logging::error("Cannot listen on port " + std::to_string(requestedPort_) + ": " + error);
        return false;
    }
    listenSocket_ = listener.release();
    boundPort_ = bound;
    if (stopRequested_)  // stop() raced with startup
        closeListener();
    listening_ = true;
    logging::info("Listening on port " + std::to_string(bound) + " - press Ctrl+C to stop");

    while (!stopRequested_) {
        const SOCKET listenSocket = listenSocket_.load();
        if (listenSocket == INVALID_SOCKET)
            break;

        const SOCKET client = ::accept(listenSocket, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (stopRequested_)
                break;
            logging::warn("accept() failed: " + net::errorMessage(WSAGetLastError()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        startSession(net::Socket(client));
        reapFinished();
    }

    listening_ = false;
    closeListener();
    shutdownAllSessions();
    logging::info("Server stopped");
    return true;
}

void ChatServer::stop() {
    stopRequested_ = true;
    closeListener();  // makes the blocking accept() in run() return
}

void ChatServer::closeListener() {
    const SOCKET s = listenSocket_.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET)
        closesocket(s);
}

void ChatServer::startSession(net::Socket socket) {
    auto session = std::make_shared<Session>();
    session->id = nextId_++;
    session->address = net::peerAddress(socket.get());
    net::setNoDelay(socket.get());
    // A client that stops reading will eventually fill its TCP window; the
    // timeout turns a forever-blocked send() into a disconnect.
    net::setSendTimeout(socket.get(), kSendTimeoutMs);
    session->socket = std::move(socket);
    logging::info("Connection #" + std::to_string(session->id) + " from " + session->address);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.push_back(session);
    }
    session->writer = std::thread(&ChatServer::writerLoop, session);
    session->reader = std::thread(&ChatServer::readerLoop, this, session);
}

void ChatServer::readerLoop(SessionPtr s) {
    if (handshake(*s)) {
        std::vector<uint8_t> frame;
        while (net::recvFrame(s->socket.get(), frame, kMaxFrameSize)) {
            const std::optional<Message> message = decode(frame);
            if (!message) {
                logging::warn(s->name + " sent a malformed frame - disconnecting");
                break;
            }
            handle(*s, *message);
        }
        leave(*s);
    }

    // Let the writer flush whatever is still queued (e.g. a Rejected reason),
    // then tear the connection down.
    s->outbox.close();
    if (s->writer.joinable())
        s->writer.join();
    s->socket.shutdownBoth();
    logging::info("Connection #" + std::to_string(s->id) + " closed");
    s->finished = true;
}

void ChatServer::writerLoop(SessionPtr s) {
    while (std::optional<Frame> frame = s->outbox.pop()) {
        if (!net::sendFrame(s->socket.get(), **frame)) {
            s->socket.shutdownBoth();  // wakes the reader so the session ends
            break;
        }
    }
}

bool ChatServer::handshake(Session& s) {
    const SOCKET sock = s.socket.get();
    net::setRecvTimeout(sock, kHandshakeTimeoutMs);  // don't let idle sockets pin a thread forever

    std::vector<uint8_t> frame;
    if (!net::recvFrame(sock, frame, kMaxFrameSize)) {
        logging::warn("Connection #" + std::to_string(s.id) + " never completed the handshake");
        return false;
    }
    const std::optional<Message> hello = decode(frame);
    if (!hello || hello->type != MsgType::Hello) {
        send(s, notice(MsgType::Rejected, "Protocol mismatch. Please use a matching client version."));
        return false;
    }
    if (!isValidUsername(hello->from)) {
        send(s, notice(MsgType::Rejected,
                       "Names must be 1-20 characters using letters, numbers, '_', '-' or '.'."));
        return false;
    }
    net::setRecvTimeout(sock, 0);

    std::lock_guard<std::mutex> lock(mutex_);
    if (findByNameLocked(hello->from)) {
        send(s, notice(MsgType::Rejected, "The name \"" + hello->from + "\" is already taken."));
        logging::warn("Rejected duplicate name \"" + hello->from + "\" from " + s.address);
        return false;
    }
    s.name = hello->from;

    Message welcome;
    welcome.type = MsgType::Welcome;
    welcome.to = s.name;
    welcome.text = serverName_;
    welcome.users = onlineUsersLocked();
    welcome.timestamp = nowMs();
    send(s, welcome);

    for (Message past : history_) {
        past.flag = true;
        send(s, past);
    }

    const std::size_t others = welcome.users.size() - 1;
    const std::string company = others == 0   ? "Nobody else is here yet."
                                : others == 1 ? "1 other person is online."
                                              : std::to_string(others) + " others are online.";
    send(s, notice(MsgType::System, "Welcome to " + serverName_ + ", " + s.name + "! " + company));

    Message joined;
    joined.type = MsgType::UserJoined;
    joined.from = s.name;
    joined.timestamp = nowMs();
    broadcastLocked(joined, &s);

    logging::info(s.name + " joined from " + s.address + " (" + plural(welcome.users.size(), "user") +
                  " online)");
    return true;
}

void ChatServer::handle(Session& s, const Message& m) {
    switch (m.type) {
    case MsgType::SendChat: {
        if (m.text.size() > kMaxTextLength) {
            send(s, notice(MsgType::Error, "That message is too long."));
            return;
        }
        Message out;
        out.type = MsgType::Chat;
        out.from = s.name;
        out.text = sanitizeText(m.text);
        out.timestamp = nowMs();
        if (out.text.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_back(out);
        if (history_.size() > kHistorySize)
            history_.pop_front();
        broadcastLocked(out);  // the echo back to the sender doubles as a delivery receipt
        logging::chat(s.name + ": " + out.text);
        return;
    }

    case MsgType::SendPrivate: {
        if (m.text.size() > kMaxTextLength) {
            send(s, notice(MsgType::Error, "That message is too long."));
            return;
        }
        Message out;
        out.type = MsgType::Private;
        out.from = s.name;
        out.text = sanitizeText(m.text);
        out.timestamp = nowMs();
        if (out.text.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        Session* target = findByNameLocked(m.to);
        if (!target) {
            send(s, notice(MsgType::Error, m.to + " is not online, so the message was not delivered."));
            return;
        }
        out.to = target->name;
        send(*target, out);
        if (target != &s)
            send(s, out);
        logging::chat(s.name + " -> " + target->name + " (direct message)");  // content stays private
        return;
    }

    case MsgType::Typing: {
        Message out;
        out.type = MsgType::Typing;
        out.from = s.name;
        out.to = m.to;
        out.flag = m.flag;

        std::lock_guard<std::mutex> lock(mutex_);
        if (m.to.empty()) {
            broadcastLocked(out, &s);
        } else if (Session* target = findByNameLocked(m.to); target && target != &s) {
            out.to = target->name;
            send(*target, out);
        }
        return;
    }

    default:
        send(s, notice(MsgType::Error, std::string("Unexpected message type: ") + toString(m.type)));
        return;
    }
}

void ChatServer::leave(Session& s) {
    std::lock_guard<std::mutex> lock(mutex_);
    Message left;
    left.type = MsgType::UserLeft;
    left.from = s.name;
    left.timestamp = nowMs();
    s.name.clear();  // drop out of the user list before telling everyone
    broadcastLocked(left);
    logging::info(left.from + " left (" + plural(onlineUsersLocked().size(), "user") + " online)");
}

void ChatServer::send(Session& s, const Message& m) {
    enqueue(s, std::make_shared<const std::vector<uint8_t>>(encode(m)));
}

void ChatServer::enqueue(Session& s, const Frame& frame) {
    // Backpressure: a client that cannot keep up gets disconnected instead of
    // making the server buffer unbounded amounts of memory for it.
    if (s.outbox.size() >= kMaxQueuedFrames) {
        s.socket.shutdownBoth();
        return;
    }
    s.outbox.push(frame);
}

void ChatServer::broadcastLocked(const Message& m, const Session* except) {
    const Frame frame = std::make_shared<const std::vector<uint8_t>>(encode(m));
    for (const SessionPtr& other : sessions_)
        if (other.get() != except && !other->name.empty())
            enqueue(*other, frame);
}

ChatServer::Session* ChatServer::findByNameLocked(const std::string& name) {
    for (const SessionPtr& s : sessions_)
        if (!s->name.empty() && sameName(s->name, name))
            return s.get();
    return nullptr;
}

std::vector<std::string> ChatServer::onlineUsersLocked() const {
    std::vector<std::string> users;
    for (const SessionPtr& s : sessions_)
        if (!s->name.empty())
            users.push_back(s->name);
    std::sort(users.begin(), users.end(), lessName);
    return users;
}

void ChatServer::reapFinished() {
    std::list<SessionPtr> done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            auto next = std::next(it);
            if ((*it)->finished)
                done.splice(done.end(), sessions_, it);
            it = next;
        }
    }
    // Join outside the lock: the threads are finished or about to return.
    for (const SessionPtr& s : done)
        if (s->reader.joinable())
            s->reader.join();
}

void ChatServer::shutdownAllSessions() {
    std::list<SessionPtr> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const SessionPtr& s : sessions_)
            s->socket.shutdownBoth();
        all.swap(sessions_);
    }
    for (const SessionPtr& s : all)
        if (s->reader.joinable())
            s->reader.join();  // each reader joins its own writer
}

}  // namespace chat

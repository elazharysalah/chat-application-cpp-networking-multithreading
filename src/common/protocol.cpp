#include "protocol.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace chat {

namespace {

class Writer {
public:
    void u8(uint8_t v) { buffer_.push_back(v); }

    void u16(uint16_t v) {
        u8(static_cast<uint8_t>(v >> 8));
        u8(static_cast<uint8_t>(v & 0xFF));
    }

    void u64(uint64_t v) {
        for (int shift = 56; shift >= 0; shift -= 8)
            u8(static_cast<uint8_t>(v >> shift));
    }

    void str(const std::string& s) {
        const auto n = static_cast<uint16_t>(std::min<std::size_t>(s.size(), 0xFFFF));
        u16(n);
        buffer_.insert(buffer_.end(), s.begin(), s.begin() + n);
    }

    std::vector<uint8_t> take() { return std::move(buffer_); }

private:
    std::vector<uint8_t> buffer_;
};

// Every read is bounds-checked: a malicious or buggy peer must never be able
// to make us read past the end of the payload.
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& buffer) : buffer_(buffer) {}

    bool u8(uint8_t& v) {
        if (remaining() < 1)
            return false;
        v = buffer_[pos_++];
        return true;
    }

    bool u16(uint16_t& v) {
        uint8_t hi = 0, lo = 0;
        if (!u8(hi) || !u8(lo))
            return false;
        v = static_cast<uint16_t>((hi << 8) | lo);
        return true;
    }

    bool u64(uint64_t& v) {
        if (remaining() < 8)
            return false;
        v = 0;
        for (int i = 0; i < 8; ++i)
            v = (v << 8) | buffer_[pos_++];
        return true;
    }

    bool str(std::string& s) {
        uint16_t n = 0;
        if (!u16(n) || remaining() < n)
            return false;
        s.assign(reinterpret_cast<const char*>(buffer_.data() + pos_), n);
        pos_ += n;
        return true;
    }

    std::size_t remaining() const { return buffer_.size() - pos_; }

private:
    const std::vector<uint8_t>& buffer_;
    std::size_t pos_ = 0;
};

bool isKnownType(uint8_t type) {
    switch (static_cast<MsgType>(type)) {
    case MsgType::Hello:
    case MsgType::SendChat:
    case MsgType::SendPrivate:
    case MsgType::Typing:
    case MsgType::Welcome:
    case MsgType::Rejected:
    case MsgType::UserJoined:
    case MsgType::UserLeft:
    case MsgType::Chat:
    case MsgType::Private:
    case MsgType::System:
    case MsgType::Error:
        return true;
    }
    return false;
}

}  // namespace

std::vector<uint8_t> encode(const Message& message) {
    Writer w;
    w.u8(kProtocolVersion);
    w.u8(static_cast<uint8_t>(message.type));
    w.u64(message.timestamp);
    w.u8(message.flag ? 1 : 0);
    w.str(message.from);
    w.str(message.to);
    w.str(message.text);
    const auto count = static_cast<uint16_t>(std::min<std::size_t>(message.users.size(), 0xFFFF));
    w.u16(count);
    for (std::size_t i = 0; i < count; ++i)
        w.str(message.users[i]);
    return w.take();
}

std::optional<Message> decode(const std::vector<uint8_t>& payload) {
    Reader r(payload);
    Message m;
    uint8_t version = 0, type = 0, flag = 0;
    uint16_t count = 0;

    if (!r.u8(version) || version != kProtocolVersion)
        return std::nullopt;
    if (!r.u8(type) || !isKnownType(type))
        return std::nullopt;
    if (!r.u64(m.timestamp) || !r.u8(flag) || !r.str(m.from) || !r.str(m.to) || !r.str(m.text) ||
        !r.u16(count))
        return std::nullopt;

    // Each user entry needs at least its 2-byte length prefix.
    if (count > r.remaining() / 2)
        return std::nullopt;
    m.users.resize(count);
    for (std::string& user : m.users)
        if (!r.str(user))
            return std::nullopt;

    if (r.remaining() != 0)
        return std::nullopt;

    m.type = static_cast<MsgType>(type);
    m.flag = flag != 0;
    return m;
}

const char* toString(MsgType type) {
    switch (type) {
    case MsgType::Hello: return "Hello";
    case MsgType::SendChat: return "SendChat";
    case MsgType::SendPrivate: return "SendPrivate";
    case MsgType::Typing: return "Typing";
    case MsgType::Welcome: return "Welcome";
    case MsgType::Rejected: return "Rejected";
    case MsgType::UserJoined: return "UserJoined";
    case MsgType::UserLeft: return "UserLeft";
    case MsgType::Chat: return "Chat";
    case MsgType::Private: return "Private";
    case MsgType::System: return "System";
    case MsgType::Error: return "Error";
    }
    return "Unknown";
}

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

bool isValidUsername(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength)
        return false;
    return std::all_of(name.begin(), name.end(), [](char ch) {
        const auto c = static_cast<unsigned char>(ch);
        return c < 128 && (std::isalnum(c) || c == '_' || c == '-' || c == '.');
    });
}

std::string sanitizeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        out.push_back(c < 0x20 || c == 0x7F ? ' ' : ch);
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos)
        return {};
    const auto last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

}  // namespace chat

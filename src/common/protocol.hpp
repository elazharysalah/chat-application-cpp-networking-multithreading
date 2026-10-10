#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Wire format
// -----------
// Every message travels as one frame:   [u32 payload length][payload]
// Payload layout (all integers big-endian, strings are [u16 length][UTF-8]):
//
//   u8 version | u8 type | u64 timestamp | u8 flag | str from | str to | str text
//   | u16 user count | str user...
//
// Using one fixed layout for every message type keeps the codec tiny;
// unused fields are simply empty.
namespace chat {

constexpr uint8_t kProtocolVersion = 1;
constexpr uint16_t kDefaultPort = 5555;
constexpr uint32_t kMaxFrameSize = 64 * 1024;
constexpr std::size_t kMaxNameLength = 20;
constexpr std::size_t kMaxTextLength = 2000;

enum class MsgType : uint8_t {
    // client -> server
    Hello = 1,        // from = requested username
    SendChat = 2,     // text, posted to #general
    SendPrivate = 3,  // to, text
    Typing = 4,       // to ("" = #general), flag = is typing. Relayed by the server with `from` filled in.

    // server -> client
    Welcome = 10,     // to = your name, users = everyone online, text = server name
    Rejected = 11,    // text = reason; the server closes the connection afterwards
    UserJoined = 12,  // from
    UserLeft = 13,    // from
    Chat = 14,        // from, text, timestamp, flag = replayed from history
    Private = 15,     // from, to, text, timestamp (sent to both participants)
    System = 16,      // text
    Error = 17,       // text
};

struct Message {
    MsgType type = MsgType::System;
    std::string from;
    std::string to;
    std::string text;
    std::vector<std::string> users;
    uint64_t timestamp = 0;  // milliseconds since the Unix epoch
    bool flag = false;
};

std::vector<uint8_t> encode(const Message& message);
// Returns nullopt for truncated, oversized, unknown or otherwise malformed payloads.
std::optional<Message> decode(const std::vector<uint8_t>& payload);

const char* toString(MsgType type);
uint64_t nowMs();

// 1-20 characters from [A-Za-z0-9_.-]
bool isValidUsername(std::string_view name);
// Replaces control characters with spaces and trims surrounding whitespace.
std::string sanitizeText(std::string_view text);

}  // namespace chat

// End-to-end tests: a real ChatServer on an ephemeral port, driven by real
// ChatClients over loopback TCP.

#include "chat_client.hpp"
#include "chat_server.hpp"
#include "log.hpp"
#include "net.hpp"
#include "protocol.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <thread>

using namespace chat;
using namespace std::chrono_literals;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* expression, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "  FAILED (line %d): %s\n", line, expression);
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __LINE__)

// Waits for the next server message matching `match`, skipping everything else.
std::optional<Message> expect(ChatClient& client, const std::function<bool(const Message&)>& match,
                              std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::optional<ClientEvent> event = client.waitEvent(50ms);
        if (event && event->kind == ClientEvent::Kind::Message && match(event->message))
            return event->message;
    }
    return std::nullopt;
}

std::optional<ClientEvent> expectEvent(ChatClient& client, ClientEvent::Kind kind,
                                       std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::optional<ClientEvent> event = client.waitEvent(50ms);
        if (event && event->kind == kind)
            return event;
    }
    return std::nullopt;
}

std::function<bool(const Message&)> is(MsgType type) {
    return [type](const Message& m) { return m.type == type; };
}

void testProtocol() {
    std::puts("[protocol] encode/decode round trip and malformed input");

    Message m;
    m.type = MsgType::Welcome;
    m.from = "alice";
    m.to = "bob";
    m.text = "h\xC3\xA9llo \xE2\x9C\x93";
    m.users = {"alice", "bob", "carol"};
    m.timestamp = 1'700'000'000'123ull;
    m.flag = true;

    const std::vector<uint8_t> bytes = encode(m);
    const std::optional<Message> decoded = decode(bytes);
    CHECK(decoded.has_value());
    if (decoded) {
        CHECK(decoded->type == m.type);
        CHECK(decoded->from == m.from);
        CHECK(decoded->to == m.to);
        CHECK(decoded->text == m.text);
        CHECK(decoded->users == m.users);
        CHECK(decoded->timestamp == m.timestamp);
        CHECK(decoded->flag == m.flag);
    }

    bool anyTruncatedAccepted = false;
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
        if (decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<long>(cut))))
            anyTruncatedAccepted = true;
    CHECK(!anyTruncatedAccepted);

    std::vector<uint8_t> trailing = bytes;
    trailing.push_back(0);
    CHECK(!decode(trailing));

    std::vector<uint8_t> wrongVersion = bytes;
    wrongVersion[0] = 99;
    CHECK(!decode(wrongVersion));

    std::vector<uint8_t> unknownType = bytes;
    unknownType[1] = 200;
    CHECK(!decode(unknownType));

    CHECK(isValidUsername("alice_1.x-y"));
    CHECK(!isValidUsername(""));
    CHECK(!isValidUsername("bad name"));
    CHECK(!isValidUsername(std::string(kMaxNameLength + 1, 'a')));
    CHECK(sanitizeText("  hi\tthere\r\n ") == "hi there");
    CHECK(sanitizeText(" \n ").empty());
}

void testConversation(uint16_t port) {
    std::puts("[server] join, history, broadcast, direct messages, typing, leave");

    ChatClient alice;
    alice.connect("127.0.0.1", port, "alice");
    const std::optional<Message> aliceWelcome = expect(alice, is(MsgType::Welcome));
    CHECK(aliceWelcome && aliceWelcome->to == "alice");
    CHECK(aliceWelcome && aliceWelcome->users == std::vector<std::string>{"alice"});

    alice.sendChat("hello history");
    CHECK(expect(alice, [](const Message& m) {
        return m.type == MsgType::Chat && m.from == "alice" && m.text == "hello history" && !m.flag;
    }));

    ChatClient bob;
    bob.connect("localhost", port, "bob");  // exercises name resolution + dual-stack listening
    const std::optional<Message> bobWelcome = expect(bob, is(MsgType::Welcome));
    CHECK(bobWelcome && bobWelcome->users == (std::vector<std::string>{"alice", "bob"}));
    const std::optional<Message> replay = expect(bob, is(MsgType::Chat));
    CHECK(replay && replay->flag && replay->text == "hello history");
    CHECK(expect(alice, [](const Message& m) { return m.type == MsgType::UserJoined && m.from == "bob"; }));

    bob.sendChat("  hi alice  ");
    CHECK(expect(alice, [](const Message& m) {
        return m.type == MsgType::Chat && m.from == "bob" && m.text == "hi alice";
    }));

    alice.sendPrivate("bob", "secret");
    CHECK(expect(bob, [](const Message& m) {
        return m.type == MsgType::Private && m.from == "alice" && m.to == "bob" && m.text == "secret";
    }));
    CHECK(expect(alice, [](const Message& m) { return m.type == MsgType::Private && m.text == "secret"; }));

    alice.sendPrivate("nobody", "hello?");
    CHECK(expect(alice, is(MsgType::Error)));

    bob.sendTyping("", true);
    CHECK(expect(alice, [](const Message& m) { return m.type == MsgType::Typing && m.from == "bob" && m.flag; }));

    std::puts("[server] rejects duplicate and invalid names");
    ChatClient imposter;
    imposter.connect("127.0.0.1", port, "ALICE");  // names are case-insensitive
    CHECK(expect(imposter, is(MsgType::Rejected)));

    ChatClient invalid;
    invalid.connect("127.0.0.1", port, "no spaces!");
    CHECK(expect(invalid, is(MsgType::Rejected)));

    bob.disconnect();
    CHECK(expect(alice, [](const Message& m) { return m.type == MsgType::UserLeft && m.from == "bob"; }));
}

void testConnectFailure() {
    std::puts("[client] reports a refused connection");
    ChatClient client;
    client.connect("127.0.0.1", 1, "nobody");
    const std::optional<ClientEvent> failed = expectEvent(client, ClientEvent::Kind::ConnectFailed, 8000ms);
    CHECK(failed && !failed->error.empty());
}

}  // namespace

int main() {
    net::WinsockSession winsock;
    if (!winsock.ok())
        return 1;
    logging::setEnabled(false);

    testProtocol();

    ChatServer server(0);
    std::thread serverThread([&server] { server.run(); });
    for (int i = 0; i < 150 && !server.isListening(); ++i)
        std::this_thread::sleep_for(20ms);
    CHECK(server.isListening());

    if (server.isListening()) {
        testConversation(server.port());
        testConnectFailure();
    }

    server.stop();
    serverThread.join();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

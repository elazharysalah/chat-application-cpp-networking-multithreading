// Minimal terminal client. Handy for testing the server without the GUI and
// for simulating several users at once.

#include "chat_client.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

void printHelp() {
    std::printf("Commands:\n"
                "  /w <name> <message>   send a direct message\n"
                "  /typing [name]        show a typing indicator in #general or to <name>\n"
                "  /users                list who is online\n"
                "  /quit                 leave\n");
}

std::string formatClock(uint64_t ms) {
    const auto seconds = static_cast<std::time_t>(ms / 1000);
    std::tm local{};
    localtime_s(&local, &seconds);
    char text[8];
    std::strftime(text, sizeof text, "%H:%M", &local);
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
        std::printf("Usage: chat_cli <name> [host] [port]\n");
        return argc < 2 ? 2 : 0;
    }
    const std::string name = argv[1];
    const std::string host = argc > 2 ? argv[2] : "localhost";
    const int port = argc > 3 ? std::atoi(argv[3]) : chat::kDefaultPort;
    if (port < 1 || port > 65535) {
        std::fprintf(stderr, "Invalid port\n");
        return 2;
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    net::WinsockSession winsock;
    if (!winsock.ok())
        return 1;

    chat::ChatClient client;
    std::atomic<bool> running{true};
    std::mutex usersMutex;
    std::vector<std::string> users;

    std::printf("Connecting to %s:%d as %s...\n", host.c_str(), port, name.c_str());
    client.connect(host, static_cast<uint16_t>(port), name);

    // The main thread blocks on stdin, so incoming messages are printed here.
    std::thread printer([&] {
        using chat::MsgType;
        while (running) {
            std::optional<chat::ClientEvent> event = client.waitEvent(std::chrono::milliseconds(200));
            if (!event)
                continue;
            if (event->kind != chat::ClientEvent::Kind::Message) {
                std::printf("*** %s\n*** Press Enter to exit.\n", event->error.c_str());
                running = false;
                break;
            }
            const chat::Message& m = event->message;
            switch (m.type) {
            case MsgType::Welcome: {
                std::lock_guard<std::mutex> lock(usersMutex);
                users = m.users;
                std::printf("*** Connected to %s as %s. Type /help for commands.\n", m.text.c_str(), m.to.c_str());
                break;
            }
            case MsgType::Rejected:
                std::printf("*** Rejected: %s\n*** Press Enter to exit.\n", m.text.c_str());
                running = false;
                break;
            case MsgType::UserJoined: {
                std::lock_guard<std::mutex> lock(usersMutex);
                users.push_back(m.from);
                std::printf("*** %s joined\n", m.from.c_str());
                break;
            }
            case MsgType::UserLeft: {
                std::lock_guard<std::mutex> lock(usersMutex);
                users.erase(std::remove(users.begin(), users.end(), m.from), users.end());
                std::printf("*** %s left\n", m.from.c_str());
                break;
            }
            case MsgType::Chat:
                std::printf("[%s] %s%s: %s\n", formatClock(m.timestamp).c_str(), m.flag ? "(earlier) " : "",
                            m.from.c_str(), m.text.c_str());
                break;
            case MsgType::Private:
                std::printf("[%s] (DM %s -> %s) %s\n", formatClock(m.timestamp).c_str(), m.from.c_str(), m.to.c_str(),
                            m.text.c_str());
                break;
            case MsgType::System:
                std::printf("*** %s\n", m.text.c_str());
                break;
            case MsgType::Error:
                std::printf("!!! %s\n", m.text.c_str());
                break;
            default:
                break;
            }
            std::fflush(stdout);
        }
    });

    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line.empty())
            continue;
        if (line == "/quit")
            break;
        if (line == "/help") {
            printHelp();
        } else if (line == "/users") {
            std::lock_guard<std::mutex> lock(usersMutex);
            std::printf("Online (%zu):", users.size());
            for (const std::string& u : users)
                std::printf(" %s", u.c_str());
            std::printf("\n");
        } else if (line == "/typing" || line.rfind("/typing ", 0) == 0) {
            client.sendTyping(line.size() > 8 ? line.substr(8) : std::string(), true);
        } else if (line.rfind("/w ", 0) == 0) {
            const std::string rest = line.substr(3);
            const auto space = rest.find(' ');
            if (space == std::string::npos)
                std::printf("Usage: /w <name> <message>\n");
            else
                client.sendPrivate(rest.substr(0, space), rest.substr(space + 1));
        } else {
            client.sendChat(line);
        }
    }

    running = false;
    client.disconnect();
    printer.join();
    return 0;
}

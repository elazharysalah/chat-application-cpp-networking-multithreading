#include "chat_server.hpp"
#include "log.hpp"
#include "net.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::atomic<chat::ChatServer*> g_server{nullptr};

// Runs on a separate thread created by the console subsystem.
BOOL WINAPI onConsoleSignal(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        if (chat::ChatServer* server = g_server.load())
            server->stop();
        return TRUE;
    }
    return FALSE;
}

void printUsage() {
    std::printf("Usage: chat_server [--port N]\n\n"
                "  --port N   TCP port to listen on (default %u, 0 = any free port)\n",
                static_cast<unsigned>(chat::kDefaultPort));
}

bool parsePort(const char* text, uint16_t& port) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 0 || value > 65535)
        return false;
    port = static_cast<uint16_t>(value);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = chat::kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        }
        const char* value = argv[i];
        if (arg == "-p" || arg == "--port") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            value = argv[++i];
        }
        if (!parsePort(value, port)) {
            std::fprintf(stderr, "Invalid port: %s\n", value);
            return 2;
        }
    }

    logging::init();
    net::WinsockSession winsock;
    if (!winsock.ok()) {
        logging::error("WSAStartup failed");
        return 1;
    }

    std::printf("\n  elazharyLearn Chat Server - ENSEM University\n  -------------------------------------------\n");
    if (port != 0) {
        std::printf("  Clients on this PC:      localhost:%u\n", static_cast<unsigned>(port));
        for (const std::string& ip : net::localIPv4Addresses())
            std::printf("  Clients on your network: %s:%u\n", ip.c_str(), static_cast<unsigned>(port));
    }
    std::printf("\n");

    chat::ChatServer server(port);
    g_server = &server;
    SetConsoleCtrlHandler(onConsoleSignal, TRUE);

    const bool ok = server.run();

    SetConsoleCtrlHandler(onConsoleSignal, FALSE);
    g_server = nullptr;
    return ok ? 0 : 1;
}

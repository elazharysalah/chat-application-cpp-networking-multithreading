#include "log.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

namespace logging {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_colors{false};

void write(const char* color, const char* tag, const std::string& message) {
    if (!g_enabled)
        return;

    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char clock[16];
    std::strftime(clock, sizeof clock, "%H:%M:%S", &local);
    const unsigned long thread = GetCurrentThreadId();

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_colors)
        std::printf("\x1b[90m%s\x1b[0m %s%-5s\x1b[0m \x1b[90mthread %-6lu\x1b[0m %s\n", clock, color, tag,
                    thread, message.c_str());
    else
        std::printf("%s %-5s thread %-6lu %s\n", clock, tag, thread, message.c_str());
    std::fflush(stdout);
}

}  // namespace

void init() {
    SetConsoleOutputCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != INVALID_HANDLE_VALUE && GetConsoleMode(out, &mode))
        g_colors = SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

void setEnabled(bool enabled) { g_enabled = enabled; }

void info(const std::string& message) { write("\x1b[36m", "INFO", message); }
void warn(const std::string& message) { write("\x1b[33m", "WARN", message); }
void error(const std::string& message) { write("\x1b[31m", "ERROR", message); }
void chat(const std::string& message) { write("\x1b[35m", "CHAT", message); }

}  // namespace logging

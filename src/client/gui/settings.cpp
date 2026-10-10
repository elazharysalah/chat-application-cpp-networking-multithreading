#include "settings.hpp"

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path settingsPath() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    std::error_code ec;
    const std::filesystem::path base =
        n > 0 && n < MAX_PATH ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path(ec);
    return base / L"elazharyLearn" / L"client.ini";
}

}  // namespace

void Settings::load() {
    std::ifstream in(settingsPath());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name") {
            name = value.substr(0, chat::kMaxNameLength);
        } else if (key == "host" && !value.empty()) {
            host = value;
        } else if (key == "port") {
            const int p = std::atoi(value.c_str());
            if (p > 0 && p <= 65535)
                port = static_cast<uint16_t>(p);
        }
    }
}

void Settings::save() const {
    const std::filesystem::path path = settingsPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << "name=" << name << "\nhost=" << host << "\nport=" << port << "\n";
}

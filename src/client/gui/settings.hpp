#pragma once

#include "protocol.hpp"

#include <cstdint>
#include <string>

// Last-used connection details, stored in %APPDATA%\elazharyLearn\client.ini.
struct Settings {
    std::string name;
    std::string host = "localhost";
    uint16_t port = chat::kDefaultPort;

    void load();
    void save() const;
};

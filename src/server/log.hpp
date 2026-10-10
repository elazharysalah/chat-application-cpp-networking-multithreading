#pragma once

#include <string>

// Thread-safe console logger. Each line carries the calling thread's id so
// the server's threading model is visible while it runs.
namespace logging {

void init();  // UTF-8 console output + ANSI colours when the console supports them
void setEnabled(bool enabled);

void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);
void chat(const std::string& message);

}  // namespace logging

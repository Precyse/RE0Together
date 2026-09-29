#pragma once

namespace logger {

// Thread-safe timestamped append to <game dir>\coop\adapter.log.
void write(const char* format, ...);

// Like write, but drops a line identical to the previous one written this way within one second.
void writeUnlessRepeated(const char* format, ...);

}  // namespace logger

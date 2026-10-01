#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "log.h"
#include "paths.h"

namespace {

constexpr int kHexBase = 16;
constexpr int kDecimalBase = 10;

const char* const kDefaultIni = "port=27960\ntrace=0\ntrace_vtables=\ncoop=0\noverlay=1\n";

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

std::vector<VtableTrace> parseVtables(const std::string& list) {
    std::vector<VtableTrace> out;
    std::stringstream items(list);
    std::string item;
    while (std::getline(items, item, ',')) {
        const auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        const uintptr_t address = std::strtoul(trim(item.substr(0, colon)).c_str(), nullptr, kHexBase);
        const unsigned count = std::strtoul(trim(item.substr(colon + 1)).c_str(), nullptr, kHexBase);
        if (address != 0 && count != 0) out.push_back({address, count});
    }
    return out;
}

}  // namespace

Config loadConfig() {
    const std::wstring path = coopDirectory() + L"\\adapter.ini";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) std::ofstream(path) << kDefaultIni;

    Config config;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "port") {
            config.port = static_cast<uint16_t>(std::strtoul(value.c_str(), nullptr, kDecimalBase));
        } else if (key == "trace") {
            config.trace = value == "1";
        } else if (key == "coop") {
            config.coop = value == "1";
        } else if (key == "overlay") {
            config.overlay = value != "0";
        } else if (key == "net_trace") {
            config.netTrace = value == "1";
        } else if (key == "trace_vtables") {
            config.traceVtables = parseVtables(value);
        }
    }
    logger::write("config: port=%u trace=%d vtables=%zu coop=%d overlay=%d net_trace=%d", config.port, config.trace,
                  config.traceVtables.size(), config.coop, config.overlay, config.netTrace);
    return config;
}

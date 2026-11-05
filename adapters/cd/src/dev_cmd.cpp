#include "dev_cmd.h"

#include <windows.h>

#include <fstream>
#include <string>

#include "link.h"
#include "log.h"
#include "paths.h"

namespace {

constexpr uint64_t kPollMs = 1000;
uint64_t g_lastPollMs = 0;

}  // namespace

namespace dev_cmd {

void tick(uint64_t nowMs) {
    if (nowMs - g_lastPollMs < kPollMs) return;
    g_lastPollMs = nowMs;
    const std::wstring path = coopDirectory() + L"\\cmd.txt";
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "marker") {
            link::sendMarker(nowMs);
        } else if (!line.empty()) {
            logger::write("dev_cmd: unknown command '%s'", line.c_str());
        }
    }
    in.close();
    DeleteFileW(path.c_str());
}

}  // namespace dev_cmd

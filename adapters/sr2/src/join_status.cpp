#include "join_status.h"

#include <windows.h>

#include <fstream>
#include <mutex>

#include "log.h"
#include "paths.h"

namespace {

std::mutex g_mutex;
std::wstring g_path;
bool g_logChanges = false;
bool g_written = false;
join_status::Status g_last;

bool writeAtomically(const std::wstring& path, const std::string& line) {
    const std::wstring temp = path + L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << line << "\n";
        if (!file) return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

}  // namespace

namespace join_status {

void open(bool logChanges) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = coopDirectory() + L"\\" + kStatusFileName;
    g_logChanges = logChanges;
}

void publish(const Status& status) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_path.empty() || (g_written && status == g_last)) return;
    const std::string line = statusLine(status);
    if (!writeAtomically(g_path, line)) {
        logger::writeUnlessRepeated("force join: cannot write the status file");
        return;
    }
    g_written = true;
    g_last = status;
    if (g_logChanges) logger::write("force join: status %s", line.c_str());
}

void writeNext() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_written = false;
}

}  // namespace join_status

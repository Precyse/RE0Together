#include "join_file.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {

constexpr unsigned kFileTimeHighShift = 32;

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return "";
    return s.substr(begin, s.find_last_not_of(" \t\r") - begin + 1);
}

bool isDigits(const std::string& s) {
    return !s.empty() && s.size() <= join_file::kMaxDigits &&
           std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool writeTimeOf(const std::wstring& path, unsigned long long& out) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return false;
    out = static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << kFileTimeHighShift |
          data.ftLastWriteTime.dwLowDateTime;
    return true;
}

}  // namespace

namespace join_file {

std::optional<JoinRequest> parse(const std::string& text) {
    JoinRequest request;
    bool role = false, token = false, password = false;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "role" && (value == "host" || value == "guest")) {
            request.role = value == "host" ? Role::Host : Role::Guest;
            role = true;
        } else if (key == "token" && isDigits(value)) {
            request.token = value;
            token = true;
        } else if (key == "password" && isDigits(value)) {
            request.password = value;
            password = true;
        } else if (key == "name" && value.size() <= join_file::kMaxNameBytes) {
            request.name = value;
        }
    }
    if (!role || !token || !password) return std::nullopt;
    return request;
}

bool Watcher::poll() {
    unsigned long long writeTime = 0;
    const bool exists = writeTimeOf(path_, writeTime);
    if (exists == exists_ && (!exists || writeTime == writeTime_)) return false;
    exists_ = exists;
    writeTime_ = writeTime;
    request_.reset();
    if (exists) {
        std::ifstream file(path_);
        std::stringstream text;
        text << file.rdbuf();
        request_ = parse(text.str());
    }
    return true;
}

}  // namespace join_file

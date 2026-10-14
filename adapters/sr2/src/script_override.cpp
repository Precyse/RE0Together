#include "script_override.h"

#include <windows.h>

#include <cstring>
#include <set>
#include <string>

#include "log.h"
#include "paths.h"
#include "script_override_rules.h"

namespace {

std::set<std::string> g_names;
bool g_trace = false;

std::string narrow(const wchar_t* text) {
    std::string out;
    for (; *text; ++text) out.push_back(*text < 0x80 ? static_cast<char>(*text) : '?');
    return out;
}

}  // namespace

namespace script_override {

size_t scan(bool trace) {
    g_trace = trace;
    g_names.clear();
    WIN32_FIND_DATAW found;
    const std::wstring pattern = gameDirectory() + L"\\coop\\scripts\\*";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        g_names.insert(script_override_rules::lowered(narrow(found.cFileName)));
    } while (FindNextFileW(search, &found));
    FindClose(search);
    if (trace) {
        for (const std::string& name : g_names) logger::write("trace: script override available: %s", name.c_str());
    }
    return g_names.size();
}

void redirect(char* path, size_t capacity) {
    const std::string replacement = script_override_rules::overridePath(path, g_names);
    if (replacement.empty() || replacement.size() >= capacity) return;
    if (g_trace) logger::write("trace: script %s opened from %s", path, replacement.c_str());
    memcpy(path, replacement.c_str(), replacement.size() + 1);
}

}  // namespace script_override

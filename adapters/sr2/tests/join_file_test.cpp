// The join file (no game): the parser, and the watcher against a real file.
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "../src/join_file.h"

namespace {

constexpr unsigned long long kHundredNsPerSecond = 10000000ULL;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void write(const std::wstring& path, const std::string& text) { std::ofstream(path, std::ios::trunc) << text; }

// Sets the file's last write time to `seconds` after a fixed start, so that a rewrite is a visible change.
void setWriteTime(const std::wstring& path, unsigned long long seconds) {
    const unsigned long long ticks = (132000000000000000ULL) + seconds * kHundredNsPerSecond;
    FILETIME time{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    SetFileTime(file, nullptr, nullptr, &time);
    CloseHandle(file);
}

}  // namespace

int main() {
    using join_file::Role;
    const auto host = join_file::parse("role=host\ntoken=109775241012345678\npassword=482913057\n");
    check(host && host->role == Role::Host && host->token == "109775241012345678" && host->password == "482913057",
          "a host file");
    const auto guest = join_file::parse("password=1\r\nrole = guest \r\ntoken=22\r\nextra=ignored\r\n");
    check(guest && guest->role == Role::Guest && guest->token == "22" && guest->password == "1",
          "keys in any order, spaces and CRLF, unknown keys ignored");
    check(!join_file::parse(""), "an empty file is no request");
    check(!join_file::parse("role=spectator\ntoken=1\npassword=1\n"), "an unknown role is no request");
    check(!join_file::parse("token=1\npassword=1\n"), "a missing role is no request");
    check(!join_file::parse("role=host\npassword=1\n"), "a missing token is no request");
    check(!join_file::parse("role=host\ntoken=1\n"), "a missing password is no request");
    check(!join_file::parse("role=host\ntoken=12ab\npassword=1\n"), "a token with letters is no request");
    check(!join_file::parse("role=host\ntoken=1\npassword=\n"), "an empty password is no request");
    check(!join_file::parse("role=host\ntoken=123456789012345678901\npassword=1\n"), "a token past 20 digits is no request");

    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    const std::wstring path = std::wstring(temp) + L"sr2_join_test_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
    DeleteFileW(path.c_str());

    join_file::Watcher watcher(path);
    check(!watcher.poll() && !watcher.request(), "no file: nothing changed, no request");

    write(path, "role=guest\ntoken=7\npassword=9\n");
    setWriteTime(path, 1);
    check(watcher.poll() && watcher.request() && watcher.request()->token == "7", "a new file is a change with its request");
    check(!watcher.poll(), "the same file is no change");

    write(path, "role=guest\ntoken=7\npassword=9\n");
    setWriteTime(path, 2);
    check(watcher.poll() && watcher.request() && watcher.request()->token == "7",
          "a rewrite with the same content is a change (the launcher's Join pressed again)");

    write(path, "role=host\ntoken=8\npassword=9\n");
    setWriteTime(path, 3);
    check(watcher.poll() && watcher.request()->role == Role::Host && watcher.request()->token == "8", "new content");

    write(path, "role=nobody\n");
    setWriteTime(path, 4);
    check(watcher.poll() && !watcher.request(), "a file without a valid role is a change to no request");

    DeleteFileW(path.c_str());
    check(watcher.poll() && !watcher.request(), "a deleted file is a change to no request");
    check(!watcher.poll(), "and then nothing changed");
    return g_failures == 0 ? 0 : 1;
}

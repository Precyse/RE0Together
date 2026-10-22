#include "watchdog.h"

#include <windows.h>

#include <dbghelp.h>

#include <cstdio>
#include <string>
#include <vector>

#include "code_patch.h"
#include "game_frame.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "partner_sites.h"
#include "paths.h"
#include "system_dll.h"
#include "thunk_frame.h"
#include "watchdog_rules.h"

namespace {

constexpr uintptr_t kGameFrame = 0x697780;  // the game's frame, called by the main loop 0x520450 at 0x5205FB, on every screen
constexpr const char* kGameFrameBytes = "8B 0D 7C 6B E9 00 83 EC";
constexpr DWORD kPollMs = 1000;
constexpr size_t kStackDwords = 16;
constexpr DWORD kThreadAccess = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION;

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
using ExitProcessFn = void(WINAPI*)(UINT);

void* g_frameOriginal = nullptr;
void* g_exitOriginal = nullptr;
HANDLE g_gameThread = nullptr;  // opened by the watch thread once the first frame names the game thread

// What the game thread was doing, read while it is suspended: nothing here allocates or takes a lock.
struct Snapshot {
    bool ok = false;
    CONTEXT context{};
    uint32_t stack[kStackDwords] = {};
};

void __cdecl onFrame(GuestFrame*) { game_frame::stamp(GetTickCount64(), GetCurrentThreadId()); }

TRACE_ENTRY_STUB(frameStub, onFrame, g_frameOriginal)

void WINAPI exitDetour(UINT code) {
    logger::write("sr2 adapter: the game exits (ExitProcess %u)", code);
    reinterpret_cast<ExitProcessFn>(g_exitOriginal)(code);
}

Snapshot capture() {
    Snapshot snapshot;
    if (!g_gameThread || SuspendThread(g_gameThread) == static_cast<DWORD>(-1)) return snapshot;
    snapshot.context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    snapshot.ok = GetThreadContext(g_gameThread, &snapshot.context) != 0;
    if (snapshot.ok) readMemory(snapshot.context.Esp, snapshot.stack, sizeof(snapshot.stack));
    ResumeThread(g_gameThread);
    return snapshot;
}

std::wstring dumpDirectory() {
    const std::wstring directory = coopDirectory() + L"\\dumps";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

// Dump names are ASCII (watchdog_rules::stem).
std::string narrow(const std::wstring& text) {
    std::string ascii;
    for (const wchar_t c : text) ascii += static_cast<char>(c);
    return ascii;
}

std::wstring wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }

// Deletes the oldest dumps (and their notes) so that the next one leaves kKeptDumps.
void makeRoom(const std::wstring& directory) {
    std::vector<std::string> stems;
    WIN32_FIND_DATAW found;
    const HANDLE search = FindFirstFileW((directory + L"\\*.dmp").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = found.cFileName;
            stems.push_back(narrow(name.substr(0, name.size() - 4)));
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    for (const std::string& old : watchdog_rules::stemsToDelete(stems, watchdog_rules::kKeptDumps)) {
        DeleteFileW((directory + L"\\" + wide(old) + L".dmp").c_str());
        DeleteFileW((directory + L"\\" + wide(old) + L".txt").c_str());
    }
}

bool writeDump(const std::wstring& path) {
    const HMODULE dbghelp = LoadLibraryW(systemDllPath(L"dbghelp.dll").c_str());
    const auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write) return false;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const BOOL written = write(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithThreadInfo, nullptr, nullptr, nullptr);
    CloseHandle(file);
    return written != 0;
}

std::string describe(const Snapshot& snapshot) {
    if (!snapshot.ok) return "registers unread";
    char text[96];
    std::snprintf(text, sizeof(text), "eip 0x%08lX esp 0x%08lX ebp 0x%08lX", snapshot.context.Eip, snapshot.context.Esp,
                  snapshot.context.Ebp);
    return text;
}

std::string stackText(const Snapshot& snapshot) {
    std::string text;
    for (size_t i = 0; i < kStackDwords && snapshot.ok; ++i) {
        char word[12];
        std::snprintf(word, sizeof(word), "%s%08X", i ? " " : "", snapshot.stack[i]);
        text += word;
    }
    return text;
}

void writeNote(const std::wstring& path, uint64_t stalledMs, const Snapshot& snapshot, bool dumped) {
    FILE* file = _wfopen(path.c_str(), L"w");
    if (!file) return;
    std::fprintf(file, "reason: the game frame 0x%X did not run for %llu ms\ngame thread: %lu\nregisters: %s\nstack top: %s\ndump: %s\n",
                 static_cast<unsigned>(kGameFrame), static_cast<unsigned long long>(stalledMs), static_cast<unsigned long>(game_frame::threadId()),
                 describe(snapshot).c_str(), stackText(snapshot).c_str(), dumped ? "written" : "failed");
    std::fclose(file);
}

// The dump and its note are written before anything is logged: a game thread stuck inside the logger would hold its lock.
void reportStall(uint64_t stalledMs) {
    const Snapshot snapshot = capture();
    const std::wstring directory = dumpDirectory();
    makeRoom(directory);
    SYSTEMTIME t;
    GetLocalTime(&t);
    const std::string stem = watchdog_rules::stem(t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const bool dumped = writeDump(directory + L"\\" + wide(stem) + L".dmp");
    writeNote(directory + L"\\" + wide(stem) + L".txt", stalledMs, snapshot, dumped);
    logger::write("watchdog: the game thread has not run a frame for %llu ms, %s, stack top %s; dump %s %s",
                  static_cast<unsigned long long>(stalledMs), describe(snapshot).c_str(), stackText(snapshot).c_str(), stem.c_str(),
                  dumped ? "written" : "failed");
}

DWORD WINAPI watchThread(LPVOID) {
    watchdog_rules::Monitor monitor;
    for (;;) {
        Sleep(kPollMs);
        if (!g_gameThread && game_frame::threadId() != 0) g_gameThread = OpenThread(kThreadAccess, FALSE, game_frame::threadId());
        const uint64_t now = GetTickCount64();
        const uint64_t lastFrame = game_frame::lastFrameMs();
        switch (monitor.step(now, lastFrame)) {
            case watchdog_rules::Event::Stalled: reportStall(now - lastFrame); break;
            case watchdog_rules::Event::Resumed:
                logger::write("watchdog: the game thread runs again after %llu ms", static_cast<unsigned long long>(monitor.stalledFor()));
                break;
            case watchdog_rules::Event::None: break;
        }
    }
}

}  // namespace

namespace watchdog {

void install() {
    if (!codeMatches({{"watchdog frame", kGameFrame, partner_sites::parseHex(kGameFrameBytes), {}}})) {
        logger::write("watchdog: the game frame differs, no watchdog");
        return;
    }
    hooks::install("watchdog frame", kGameFrame, reinterpret_cast<void*>(&frameStub), &g_frameOriginal);
    const FARPROC exitProcess = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ExitProcess");
    if (exitProcess) hooks::install("exit", reinterpret_cast<uintptr_t>(exitProcess), reinterpret_cast<void*>(&exitDetour), &g_exitOriginal);
    if (HANDLE thread = CreateThread(nullptr, 0, watchThread, nullptr, 0, nullptr)) CloseHandle(thread);
    logger::write("watchdog: a game thread stall of %llu ms writes coop\\dumps, the newest %zu kept",
                  static_cast<unsigned long long>(watchdog_rules::kStallMs), watchdog_rules::kKeptDumps);
}

}  // namespace watchdog

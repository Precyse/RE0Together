#include "crash_dump.h"

#include <windows.h>

#include <dbghelp.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <string>

#include "hang_rule.h"
#include "log.h"
#include "paths.h"

namespace {

// Thread and module lists plus the stacks and the data they point at: enough to walk every thread offline.
constexpr MINIDUMP_TYPE kDumpType =
    static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo);
constexpr size_t kNameLength = 64;
constexpr size_t kLineLength = 160;
constexpr ULONG_PTR kAccessWrite = 1;    // EXCEPTION_RECORD::ExceptionInformation[0] of an access violation
constexpr ULONG_PTR kAccessExecute = 8;
constexpr DWORD kHangCode = 0xE0484E47;  // "HNG" after the E0 prefix: the synthetic exception code of a hang dump
constexpr DWORD kHangPollMs = 1000;
constexpr uint32_t kMsPerSecond = 1000;
constexpr uint32_t kHangSeconds = hang_rule::kHangMs / kMsPerSecond;
constexpr size_t kStackWords = 16;        // logged from the hung thread's stack pointer
constexpr size_t kStackLineLength = 256;

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
std::atomic<bool> g_writing{false};       // a crash inside the dump writer must not recurse
std::atomic<bool> g_caughtDumped{false};  // one dump of a caught exception per session
std::atomic<uint32_t> g_lastBeatMs{0};     // GetTickCount() of the last drawn frame, 0 before the first
std::atomic<crash_dump::Foreground> g_foreground{nullptr};  // the adapter's "game window in front" test
std::atomic<DWORD> g_frameThread{0};       // the thread of the last heartbeat
std::atomic<bool> g_threadsLogged{false};

std::wstring dumpPath(const wchar_t* prefix) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[kNameLength];
    swprintf(name, kNameLength, L"\\%ls-%04u%02u%02u-%02u%02u%02u.dmp", prefix, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    return coopDirectory() + name;
}

// Writes the dump, naming `threadId` as the thread that raised `info`, and logs "<what>, dump written to <path>".
void writeDump(EXCEPTION_POINTERS* info, DWORD threadId, const wchar_t* prefix, const char* what) {
    const std::wstring path = dumpPath(prefix);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION exception{threadId, info, FALSE};
    const BOOL written =
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType, &exception, nullptr, nullptr);
    CloseHandle(file);
    logger::write("crash_dump: %s, dump %s %ls", what, written ? "written to" : "failed for", path.c_str());
}

// "re0hd.exe+0x200df", or "no module" for heap or unmapped code.
std::string moduleOffset(const void* address) {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &module) ||
        !GetModuleFileNameW(module, path, MAX_PATH)) {
        return "no module";
    }
    const wchar_t* slash = wcsrchr(path, L'\\');
    char text[kLineLength];
    snprintf(text, sizeof(text), "%ls+0x%x", slash ? slash + 1 : path,
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)));
    return text;
}

// ", reading 0x1234" for an access violation, empty otherwise.
std::string accessDetail(const EXCEPTION_RECORD& record) {
    if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record.NumberParameters < 2) return {};
    const ULONG_PTR kind = record.ExceptionInformation[0];
    const char* verb = kind == kAccessWrite ? "writing" : kind == kAccessExecute ? "executing" : "reading";
    char text[kLineLength];
    snprintf(text, sizeof(text), ", %s 0x%p", verb, reinterpret_cast<void*>(record.ExceptionInformation[1]));
    return text;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    if (!g_writing.exchange(true)) {
        char what[kLineLength];
        snprintf(what, sizeof(what), "exception 0x%08lx at 0x%p", info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
        writeDump(info, GetCurrentThreadId(), L"crash", what);
    }
    return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

// The registers of a thread caught mid-stall and the words on top of its stack.
struct StalledThread {
    CONTEXT context;
    std::array<DWORD, kStackWords> stack;
    size_t stackWords;
};

// The stack and instruction pointers of a thread context (the DS2 adapter shares this file and builds x64).
uintptr_t stackPointer(const CONTEXT& context) {
#ifdef _WIN64
    return context.Rsp;
#else
    return context.Esp;
#endif
}

uintptr_t instructionPointer(const CONTEXT& context) {
#ifdef _WIN64
    return context.Rip;
#else
    return context.Eip;
#endif
}

// Suspends `threadId` just long enough to read its registers and stack; resumed before anything logs or dumps, as the
// suspended thread may hold the log's lock or the heap's.
bool freeze(DWORD threadId, StalledThread& out) {
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (!thread) return false;
    out.context = {};
    out.context.ContextFlags = CONTEXT_FULL;
    out.stackWords = 0;
    bool read = false;
    if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
        read = GetThreadContext(thread, &out.context) != 0;
        SIZE_T got = 0;
        if (read && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(stackPointer(out.context)),
                                      out.stack.data(), sizeof(out.stack), &got)) {
            out.stackWords = got / sizeof(DWORD);
        }
        ResumeThread(thread);
    }
    CloseHandle(thread);
    return read;
}

void logStack(const StalledThread& stalled) {
    char line[kStackLineLength];
    int length = snprintf(line, sizeof(line), "crash_dump: stack at 0x%08zx:", stackPointer(stalled.context));
    for (size_t i = 0; i < stalled.stackWords && length > 0 && static_cast<size_t>(length) < sizeof(line); ++i) {
        length += snprintf(line + length, sizeof(line) - length, " %08lx", stalled.stack[i]);
    }
    logger::write("%s", line);
}

// The stall's minidump: the hung thread is the "exception" thread, stopped at its current instruction.
void dumpHang(DWORD threadId) {
    StalledThread stalled;
    if (!freeze(threadId, stalled)) {
        logger::write("crash_dump: no frame for %u s, thread %lu unreadable, no dump", kHangSeconds, threadId);
        return;
    }
    EXCEPTION_RECORD record{};
    record.ExceptionCode = kHangCode;
    record.ExceptionAddress = reinterpret_cast<PVOID>(instructionPointer(stalled.context));
    EXCEPTION_POINTERS pointers{&record, &stalled.context};
    char what[kLineLength];
    snprintf(what, sizeof(what), "no frame for %u s (thread %lu ip 0x%08zx = %s)", kHangSeconds, threadId,
             instructionPointer(stalled.context), moduleOffset(record.ExceptionAddress).c_str());
    writeDump(&pointers, threadId, L"hang", what);
    logStack(stalled);
}

DWORD WINAPI hangWatchThread(LPVOID) {
    hang_rule::HangWatch watch;
    uint32_t seenBeat = 0;
    for (;;) {
        Sleep(kHangPollMs);
        const uint32_t beat = g_lastBeatMs.load(std::memory_order_relaxed);
        if (beat != 0 && beat != seenBeat) {
            seenBeat = beat;
            watch.beat(beat);
        }
        if (watch.check(GetTickCount(), g_foreground.load(std::memory_order_relaxed)()) == hang_rule::Verdict::Dump) {
            dumpHang(g_frameThread.load(std::memory_order_relaxed));
        }
    }
}

// Once the game tick has a thread: names both, so the log shows whether the frame is drawn on the game thread.
void logThreads(DWORD frameThread, DWORD gameThread) {
    if (!gameThread || g_threadsLogged.exchange(true)) return;
    logger::write("crash_dump: frame thread %lu, game tick thread %lu", frameThread, gameThread);
}

}  // namespace

namespace crash_dump {

void install() {
    const LPTOP_LEVEL_EXCEPTION_FILTER replaced = SetUnhandledExceptionFilter(onCrash);
    if (replaced == onCrash) return;
    g_previous = replaced;
    logger::write("crash_dump: installed");
}

LONG reportCaught(EXCEPTION_POINTERS* info, const char* what) {
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    logger::write("%s raised exception 0x%08lx at 0x%p (%s)%s", what, record.ExceptionCode, record.ExceptionAddress,
                  moduleOffset(record.ExceptionAddress).c_str(), accessDetail(record).c_str());
    if (!g_caughtDumped.exchange(true)) writeDump(info, GetCurrentThreadId(), L"caught", "caught exception");
    return EXCEPTION_EXECUTE_HANDLER;
}

void heartbeat(DWORD gameThread) {
    const DWORD thread = GetCurrentThreadId();
    g_frameThread.store(thread, std::memory_order_relaxed);
    g_lastBeatMs.store(GetTickCount(), std::memory_order_relaxed);
    if (!g_threadsLogged.load(std::memory_order_relaxed)) logThreads(thread, gameThread);
}

void startHangWatch(Foreground foreground) {
    g_foreground.store(foreground, std::memory_order_relaxed);
    HANDLE thread = CreateThread(nullptr, 0, hangWatchThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
}

}  // namespace crash_dump

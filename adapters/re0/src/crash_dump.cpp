#include "crash_dump.h"

#include <windows.h>

#include <dbghelp.h>

#include <atomic>
#include <cwchar>
#include <string>

#include "log.h"
#include "paths.h"

namespace {

// Thread and module lists plus the stacks and the data they point at: enough to walk every thread offline.
constexpr MINIDUMP_TYPE kDumpType =
    static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo);
constexpr size_t kNameLength = 64;

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
std::atomic<bool> g_writing{false};  // a crash inside the dump writer must not recurse

std::wstring dumpPath() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[kNameLength];
    swprintf(name, kNameLength, L"\\crash-%04u%02u%02u-%02u%02u%02u.dmp", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    return coopDirectory() + name;
}

LONG WINAPI onCrash(EXCEPTION_POINTERS* info) {
    if (!g_writing.exchange(true)) {
        const std::wstring path = dumpPath();
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
            const BOOL written =
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, kDumpType, &exception, nullptr, nullptr);
            CloseHandle(file);
            logger::write("crash_dump: exception 0x%08lx at 0x%p, dump %s %ls", info->ExceptionRecord->ExceptionCode,
                          info->ExceptionRecord->ExceptionAddress, written ? "written to" : "failed for", path.c_str());
        }
    }
    return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace crash_dump {

void install() {
    const LPTOP_LEVEL_EXCEPTION_FILTER replaced = SetUnhandledExceptionFilter(onCrash);
    if (replaced == onCrash) return;
    g_previous = replaced;
    logger::write("crash_dump: installed");
}

}  // namespace crash_dump

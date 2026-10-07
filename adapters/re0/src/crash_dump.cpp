#include "crash_dump.h"

#include <windows.h>

#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <string>

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

LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;
std::atomic<bool> g_writing{false};       // a crash inside the dump writer must not recurse
std::atomic<bool> g_caughtDumped{false};  // one dump of a caught exception per session

std::wstring dumpPath(const wchar_t* prefix) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[kNameLength];
    swprintf(name, kNameLength, L"\\%ls-%04u%02u%02u-%02u%02u%02u.dmp", prefix, t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond);
    return coopDirectory() + name;
}

// Writes the dump and logs "<what> ... dump written to <path>".
void writeDump(EXCEPTION_POINTERS* info, const wchar_t* prefix, const char* what) {
    const std::wstring path = dumpPath(prefix);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
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
        writeDump(info, L"crash", what);
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

LONG reportCaught(EXCEPTION_POINTERS* info, const char* what) {
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    logger::write("%s raised exception 0x%08lx at 0x%p (%s)%s", what, record.ExceptionCode, record.ExceptionAddress,
                  moduleOffset(record.ExceptionAddress).c_str(), accessDetail(record).c_str());
    if (!g_caughtDumped.exchange(true)) writeDump(info, L"caught", "caught exception");
    return EXCEPTION_EXECUTE_HANDLER;
}

}  // namespace crash_dump

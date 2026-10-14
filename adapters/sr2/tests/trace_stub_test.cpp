// The entry and return trace stubs of thunk_frame.h against a stand-in game function: the arguments the trace sees, the
// function's own result and stack balance, the swapped return address and the registers a caller keeps.
#include <cstdint>
#include <cstdio>

#include "../src/thunk_frame.h"

namespace {

// Macros, because inline assembly cannot read constexpr values.
#define kFirst 0x11
#define kSecond 0x22
#define kThird 0x33
#define kRegisterMark 0xC0FFEE

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

uint32_t __stdcall fakeAdd(uint32_t a, uint32_t b, uint32_t c) { return a + b + c; }

void* g_original = reinterpret_cast<void*>(&fakeAdd);

uint32_t g_seenArgs[3];
uint32_t g_seenReturn = 0;
uint32_t g_seenResult = 0;
uint32_t g_realReturn = 0;
int g_entries = 0;
int g_exits = 0;

void __cdecl onEntry(GuestFrame* frame) {
    ++g_entries;
    for (int i = 0; i < 3; ++i) g_seenArgs[i] = (&frame->args[0])[i];
    g_seenReturn = frame->returnAddress;
}

TRACE_ENTRY_STUB(entryStub, onEntry, g_original)

void exitStub();

void __cdecl onReturnEntry(GuestFrame* frame) {
    g_realReturn = frame->returnAddress;
    frame->returnAddress = reinterpret_cast<uint32_t>(&exitStub);
}

uint32_t __cdecl onReturnLeave(uint32_t result) {
    ++g_exits;
    g_seenResult = result;
    return g_realReturn;
}

TRACE_RETURN_STUBS(returnStub, exitStub, onReturnEntry, onReturnLeave, g_original)

using AddFn = uint32_t(__stdcall*)(uint32_t, uint32_t, uint32_t);

// Calls through a stub with values in the registers a caller expects to keep, and reports whether they survived.
template <typename Stub>
uint32_t callKeepingRegisters(Stub stub, bool& registersKept) {
    uint32_t result = 0;
    uint32_t kept = 0;
    __asm {
        mov ebx, kRegisterMark
        mov esi, kRegisterMark
        mov edi, kRegisterMark
        push kThird
        push kSecond
        push kFirst
        mov eax, stub
        call eax
        mov result, eax
        xor ecx, ecx
        cmp ebx, kRegisterMark
        jne lost
        cmp esi, kRegisterMark
        jne lost
        cmp edi, kRegisterMark
        jne lost
        mov ecx, 1
    lost:
        mov kept, ecx
    }
    registersKept = kept == 1;
    return result;
}

}  // namespace

int main() {
    bool kept = false;
    const uint32_t entryResult = callKeepingRegisters(reinterpret_cast<AddFn>(&entryStub), kept);
    check(entryResult == kFirst + kSecond + kThird, "entry stub: the function's result is unchanged");
    check(g_entries == 1 && g_seenArgs[0] == kFirst && g_seenArgs[1] == kSecond && g_seenArgs[2] == kThird,
          "entry stub: the trace sees the stack arguments in order");
    check(g_seenReturn != 0 && kept, "entry stub: the return address is the caller's and the caller's registers survive");

    const uint32_t returnResult = callKeepingRegisters(reinterpret_cast<AddFn>(&returnStub), kept);
    check(returnResult == kFirst + kSecond + kThird, "return stub: the function's result reaches the caller");
    check(g_exits == 1 && g_seenResult == kFirst + kSecond + kThird, "return stub: the trace sees the result");
    check(g_realReturn != 0 && kept, "return stub: it returns to the real caller with its registers kept");
    return g_failures == 0 ? 0 : 1;
}

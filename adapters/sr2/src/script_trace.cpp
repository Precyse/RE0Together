#include "script_trace.h"

#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>

#include "fpu_guard.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

using partner_sites::Thunk;
using script_trace_rules::FadeSnapshot;

constexpr size_t kThreadNameLimit = 64;
constexpr size_t kMessageLimit = 256;
constexpr const char* kUnknownThread = "?";
constexpr const char* kNoString = "(no string)";

void* g_fadeInOriginal = nullptr;
void* g_fadeOutOriginal = nullptr;
void* g_fadeTickOriginal = nullptr;
const uintptr_t g_luaResume = sr2::kLuaResume;
const uintptr_t g_luaCallAlert = sr2::kLuaCallAlert;

std::mutex g_errorMutex;
script_trace_rules::NamedRateLimit g_errorLimit;
script_trace_rules::FadeWatch g_fadeWatch;

// lua_tostring(L, -1): after a failed resume or load the error object is on top of the stack.
std::string errorMessage(uint32_t lua) {
    guest_call::Regs regs{};
    regs.ecx = lua;
    regs.eax = static_cast<uint32_t>(sr2::kLuaStackTop);
    const uint32_t text = guest_call::call(sr2::kLuaToString, regs);
    if (!text) return kNoString;
    char buffer[kMessageLimit + 1];
    const size_t length = readCString(text, buffer, sizeof(buffer));
    return script_trace_rules::clipped(buffer, length, kMessageLimit);
}

// The name the thread was started with, "?" when there is none or it is not printable.
std::string threadName(uint32_t record) {
    const uint32_t name = record ? readDword(record + sr2::kLuaThreadNameOffset) : 0;
    char buffer[kThreadNameLimit + 1] = {};
    const size_t length = name ? readCString(name, buffer, sizeof(buffer)) : 0;
    return script_trace_rules::isPrintableName(buffer, length + 1) ? std::string(buffer, length) : kUnknownThread;
}

void logLuaError(const std::string& subject, uint32_t lua) {
    uint32_t triggers = 0;
    {
        std::lock_guard<std::mutex> lock(g_errorMutex);
        if (!g_errorLimit.allow(subject, GetTickCount64(), triggers)) return;
    }
    logger::write("trace: lua error: %s: %s", subject.c_str(), errorMessage(lua).c_str());
}

// CE1: lua_resume returned an error code for the thread runner's thread; L was in esi, edi points to the record pointer.
void __cdecl traceThreadError(uint32_t lua, uint32_t recordSlot) {
    logLuaError("thread " + threadName(readDword(recordSlot)), lua);
}

// CE2: callalert(L) is about to report a load error.
void __cdecl traceLoadError(uint32_t lua) { logLuaError("load", lua); }

// The thread runner's `call lua_resume`: cdecl(nargs) with esi = L, the caller pops the argument. L is kept in ecx across the
// call (the original code needs only edi after it); eax is the game's result.
void __declspec(naked) luaThreadResumeStub() {
    __asm {
        push esi
        push dword ptr [esp + 8]
        call dword ptr [g_luaResume]
        add esp, 4
        pop ecx
        test eax, eax
        jz finished
        pushad
        push edi
        push ecx
        call traceThreadError
        add esp, 8
        popad
    finished:
        ret
    }
}

// callalert takes L in eax; the log comes first, then the game's own alert returns to the caller.
void __declspec(naked) luaLoadErrorStub() {
    __asm {
        pushad
        push eax
        call traceLoadError
        add esp, 4
        popad
        jmp dword ptr [g_luaCallAlert]
    }
}

// A traced fade call: the return address it was entered with and the fade before it. A fade function does not call itself.
struct Pending {
    uint32_t returnAddress = 0;
    FadeSnapshot before;
};

thread_local Pending t_fadeIn;
thread_local Pending t_fadeOut;
thread_local Pending t_fadeTick;

void exitFadeInStub();
void exitFadeOutStub();
void exitFadeTickStub();

void redirectReturn(GuestFrame* frame, Pending& pending, void (*exitStub)()) {
    if (pending.returnAddress) return;
    pending.returnAddress = frame->returnAddress;
    pending.before = script_trace::readFade();
    frame->returnAddress = reinterpret_cast<uint32_t>(exitStub);
}

uint32_t takeReturn(Pending& pending) {
    const uint32_t returnAddress = pending.returnAddress;
    pending.returnAddress = 0;
    return returnAddress;
}

void logScriptFade(const char* name, const Pending& pending) {
    const FadeSnapshot after = script_trace::readFade();
    logger::write("trace: fade: script %s, state %u -> %u, percent %.3f -> %.3f, pause count %d, ignore pause %u", name, pending.before.state,
                  after.state, pending.before.percent, after.percent, after.pauseCount, after.ignorePause);
}

// FD1, FD2: the Lua natives fade_in and fade_out.
void __cdecl enterFadeIn(GuestFrame* frame) { redirectReturn(frame, t_fadeIn, &exitFadeInStub); }
void __cdecl enterFadeOut(GuestFrame* frame) { redirectReturn(frame, t_fadeOut, &exitFadeOutStub); }

uint32_t __cdecl leaveFadeIn(uint32_t) {
    FpuGuard guard;
    logScriptFade("fade_in", t_fadeIn);
    return takeReturn(t_fadeIn);
}

uint32_t __cdecl leaveFadeOut(uint32_t) {
    FpuGuard guard;
    logScriptFade("fade_out", t_fadeOut);
    return takeReturn(t_fadeOut);
}

// FD3: the fade tick, every frame; only the edges are written.
void __cdecl enterFadeTick(GuestFrame* frame) { redirectReturn(frame, t_fadeTick, &exitFadeTickStub); }

uint32_t __cdecl leaveFadeTick(uint32_t) {
    FpuGuard guard;
    const FadeSnapshot now = script_trace::readFade();
    const script_trace_rules::FadeEdges edges = g_fadeWatch.observe(now);
    if (edges.stateChanged) logger::write("trace: fade: state %u -> %u", edges.stateBefore, now.state);
    if (edges.complete) logger::write("trace: fade: complete at percent %.3f", now.percent);
    if (edges.held) logger::write("trace: fade: held by the pause count %d", now.pauseCount);
    if (edges.released) logger::write("trace: fade: released");
    return takeReturn(t_fadeTick);
}

TRACE_RETURN_STUBS(fadeInStub, exitFadeInStub, enterFadeIn, leaveFadeIn, g_fadeInOriginal)
TRACE_RETURN_STUBS(fadeOutStub, exitFadeOutStub, enterFadeOut, leaveFadeOut, g_fadeOutOriginal)
TRACE_RETURN_STUBS(fadeTickStub, exitFadeTickStub, enterFadeTick, leaveFadeTick, g_fadeTickOriginal)

}  // namespace

namespace script_trace {

FadeSnapshot readFade() {
    FadeSnapshot fade;
    fade.state = readDword(sr2::kFadeState);
    const uint32_t percentBits = readDword(sr2::kFadePercent);
    std::memcpy(&fade.percent, &percentBits, sizeof(fade.percent));
    fade.timer = static_cast<int32_t>(readDword(sr2::kFadeTimer));
    fade.pauseCount = static_cast<int32_t>(readDword(sr2::kPauseRefCount));
    fade.ignorePause = readByte(sr2::kFadeIgnorePause);
    return fade;
}

partner_thunks::Binding bind(Thunk thunk) {
    switch (thunk) {
        case Thunk::LuaThreadResume: return {reinterpret_cast<void*>(&luaThreadResumeStub), nullptr};
        case Thunk::LuaLoadError: return {reinterpret_cast<void*>(&luaLoadErrorStub), nullptr};
        case Thunk::TraceFadeIn: return {reinterpret_cast<void*>(&fadeInStub), &g_fadeInOriginal};
        case Thunk::TraceFadeOut: return {reinterpret_cast<void*>(&fadeOutStub), &g_fadeOutOriginal};
        case Thunk::TraceFadeTick: return {reinterpret_cast<void*>(&fadeTickStub), &g_fadeTickOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace script_trace

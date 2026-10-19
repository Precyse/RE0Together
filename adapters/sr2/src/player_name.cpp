#include "player_name.h"

#include <windows.h>

#include <mutex>
#include <optional>

#include "code_patch.h"
#include "guest_call.h"
#include "hooks.h"
#include "log.h"
#include "memory.h"
#include "partner_sites.h"
#include "player_name_rules.h"
#include "sr2_addresses.h"
#include "thunk_frame.h"

namespace {

constexpr size_t kCapArg = 0;
constexpr uint32_t kReturnTrue = 1;
constexpr uint32_t kLowByteMask = 0xFF;
constexpr size_t kUtf8MaxBytes = 4;

void* g_original = nullptr;
bool g_trace = false;

std::mutex g_mutex;  // the join file is read on the front end thread, the builder runs on the game thread
std::u16string g_name;
std::optional<std::string> g_lastSet;

std::string utf8Of(const wchar_t* text, size_t length) {
    char buffer[player_name_rules::kBufferChars * kUtf8MaxBytes] = {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), buffer, sizeof(buffer) - 1, nullptr, nullptr);
    return std::string(buffer, bytes > 0 ? static_cast<size_t>(bytes) : 0);
}

std::string gameNameAt(uintptr_t wide) {
    wchar_t text[player_name_rules::kBufferChars] = {};
    readMemory(wide, text, sizeof(text) - sizeof(wchar_t));
    return utf8Of(text, wcslen(text));
}

// Writes the name and its terminator to the wide destination; the caller has cut it to what fits.
void writeName(uintptr_t destination, const std::u16string& name) {
    wchar_t* out = reinterpret_cast<wchar_t*>(destination);
    for (size_t i = 0; i < name.size(); ++i) out[i] = static_cast<wchar_t>(name[i]);
    out[name.size()] = L'\0';
}

// cdecl(cap) with edi = the wide destination, AL = 1 on return.
void __cdecl onLocalName(GuestFrame* frame) {
    const uint32_t cap = frame->args[kCapArg];
    std::u16string name;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        name = player_name_rules::fit(g_name, cap);
    }
    const bool launcher = !name.empty();
    if (launcher) {
        writeName(frame->regs.edi, name);
        frame->regs.eax = (frame->regs.eax & ~kLowByteMask) | kReturnTrue;
    } else {
        guest_call::callWithRegs(reinterpret_cast<uintptr_t>(g_original), &frame->regs, frame->args, 1);
    }
    if (!g_trace) return;
    const std::string shown = gameNameAt(frame->regs.edi);
    logger::write("trace: name: local member named \"%s\" (%s)", shown.c_str(), launcher ? "launcher" : "game");
}

PARTNER_WRAP_STUB(localNameStub, onLocalName, 0)

}  // namespace

namespace player_name {

void set(const std::string& utf8) {
    if (g_lastSet == utf8) return;
    g_lastSet = utf8;
    const std::optional<std::u16string> parsed = player_name_rules::parse(utf8);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_name = parsed.value_or(std::u16string());
    }
    if (parsed) {
        logger::write("player name: launcher name \"%s\"", utf8.c_str());
    } else if (utf8.empty()) {
        logger::write("player name: none from the launcher, game name kept");
    } else {
        logger::write("player name: the launcher's name is outside the allowed set or too long, game name kept");
    }
}

bool install(bool trace) {
    const CodePatch entry{"player name", sr2::kLocalNameBuilder, partner_sites::parseHex(sr2::kLocalNameBuilderBytes), {}};
    if (!codeMatches({entry})) {
        logger::write("player name: site differs, game names kept");
        return false;
    }
    g_trace = trace;
    return hooks::install("player_name_builder", sr2::kLocalNameBuilder, reinterpret_cast<void*>(&localNameStub), &g_original);
}

}  // namespace player_name

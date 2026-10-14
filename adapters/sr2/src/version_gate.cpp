#include "version_gate.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

#include "code_patch.h"
#include "guest_call.h"
#include "log.h"
#include "memory.h"
#include "trailer_rules.h"
#include "version_gate_patches.h"

namespace {

using trailer_rules::Verdict;

constexpr size_t kMaxPayload = 1300;      // the game's message buffers
constexpr uintptr_t kPayloadLengthBack = 2;  // the u16 payload length sits just before the payload (3 byte header)
constexpr uintptr_t kContextMemberOffset = 4;   // ctx[1]: the member the sender is mapped to, 0 for a stranger
constexpr uintptr_t kSenderPortOffset = 4;      // in the sender address block (the IP is at 0)
constexpr size_t kAddressBlockWords = 5;        // {ip, port, 0, 0, 0}
constexpr uint32_t kGameVersion = 0xC9;         // the first field of a join_deny
constexpr uint8_t kReasonWrongVersion = 1;      // the game's own "wrong game version"
constexpr size_t kDenyPayloadWords = 2;

trailer_rules::Settings g_settings;
bool g_trace = false;
const uintptr_t g_send = version_gate_patches::kSend;

using HandlerFn = void(__cdecl*)(uintptr_t payload, uintptr_t ctx);

void callHandler(uintptr_t handler, uintptr_t payload, uintptr_t ctx) { reinterpret_cast<HandlerFn>(handler)(payload, ctx); }

uint16_t readWord(uintptr_t address) {
    uint16_t value = 0;
    readMemory(address, &value, sizeof(value));
    return value;
}

struct Inspection {
    Verdict verdict;
    std::optional<trailer_rules::Trailer> theirs;
};

// The trailer at the end of the payload the handler is about to parse.
Inspection inspect(uintptr_t payload) {
    const size_t length = readWord(payload - kPayloadLengthBack);
    std::vector<uint8_t> bytes(length <= kMaxPayload ? length : 0);
    if (!bytes.empty() && !readMemory(payload, bytes.data(), bytes.size())) bytes.clear();
    return {trailer_rules::check(bytes.data(), bytes.size(), g_settings), trailer_rules::read(bytes.data(), bytes.size())};
}

// The words of a log line that say why a message was refused.
void logRefusal(const char* who, uint32_t ip, uint16_t port, const Inspection& inspection) {
    const auto* octets = reinterpret_cast<const uint8_t*>(&ip);
    char why[96];
    if (inspection.theirs) {
        snprintf(why, sizeof(why), "version %u players %u flags %u, ours version %u players %u flags %u",
                 inspection.theirs->protocolVersion, inspection.theirs->maxPlayers, inspection.theirs->flags,
                 trailer_rules::ours(g_settings).protocolVersion, g_settings.maxPlayers,
                 trailer_rules::ours(g_settings).flags);
    } else {
        snprintf(why, sizeof(why), "no mod");
    }
    logger::write("version gate: %s %u.%u.%u.%u:%u refused, %s", who, octets[0], octets[1], octets[2], octets[3], port, why);
}

// G: host. A join_req without our trailer gets join_deny reason 1, to the real sender, and nothing else happens.
void __cdecl gateJoinReq(uintptr_t payload, uintptr_t ctx) {
    if (readDword(ctx + kContextMemberOffset) != 0) {  // the sender has a member: the handler drops it itself
        callHandler(version_gate_patches::kJoinReqHandler, payload, ctx);
        return;
    }
    const Inspection inspection = inspect(payload);
    const uintptr_t sender = readDword(ctx);
    const uint32_t ip = readDword(sender);
    const uint32_t port = readDword(sender + kSenderPortOffset);
    if (inspection.verdict == Verdict::Match) {
        if (g_trace) logger::write("version gate: join_req from 0x%08X:%u accepted", ip, port);
        callHandler(version_gate_patches::kJoinReqHandler, payload, ctx);
        return;
    }
    logRefusal("join_req from", ip, static_cast<uint16_t>(port), inspection);
    uint32_t block[kAddressBlockWords] = {ip, port, 0, 0, 0};
    guest_call::call(version_gate_patches::kJoinDenySender, {.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block))},
                     {kReasonWrongVersion});
}

// G: guest. A join_acc without our trailer is refused through the game's own join_deny handler.
void __cdecl gateJoinAcc(uintptr_t payload, uintptr_t ctx) {
    const Inspection inspection = inspect(payload);
    if (inspection.verdict == Verdict::Match) {
        if (g_trace) logger::write("version gate: join_acc accepted");
        callHandler(version_gate_patches::kJoinAccHandler, payload, ctx);
        return;
    }
    const uintptr_t sender = readDword(ctx);
    logRefusal("join_acc from host", readDword(sender), readWord(sender + kSenderPortOffset), inspection);
    uint32_t deny[kDenyPayloadWords] = {kGameVersion, kReasonWrongVersion};
    callHandler(version_gate_patches::kJoinDenyHandler, reinterpret_cast<uintptr_t>(deny), ctx);
}

// V3, V4: `args` are the stack arguments of the game's send, (buffer, length, 0, 0). The trailer goes behind the message
// and both lengths grow; a message that would not fit goes out as it is (and is refused by the other side).
void __cdecl appendTrailer(uint32_t* args) {
    auto* buffer = reinterpret_cast<uint8_t*>(args[0]);
    const size_t length = args[1];
    if (!trailer_rules::fitsInSend(length)) {
        logger::write("version gate: a message of %zu bytes has no room for the trailer", length);
        return;
    }
    const auto trailer = trailer_rules::make(g_settings);
    memcpy(buffer + length, trailer.data(), trailer.size());
    uint16_t payload = 0;
    memcpy(&payload, buffer + 1, sizeof(payload));
    payload = static_cast<uint16_t>(payload + trailer.size());
    memcpy(buffer + 1, &payload, sizeof(payload));
    args[1] = static_cast<uint32_t>(length + trailer.size());
}

// Stands in for the call of the game's send in the join_req and join_acc senders: it keeps every register but eax, ecx
// and edx (the send takes only esi), changes the arguments in place and goes on to the send, which returns to the sender.
void __declspec(naked) sendWithTrailerStub() {
    __asm {
        lea eax, [esp + 4]
        push eax
        call appendTrailer
        add esp, 4
        jmp dword ptr [g_send]
    }
}

}  // namespace

namespace version_gate {

bool install(uint32_t maxPlayers, bool partnerExperimental, bool trace) {
    g_trace = trace;
    g_settings = {static_cast<uint8_t>(maxPlayers), partnerExperimental};
    const version_gate_patches::Thunks thunks{reinterpret_cast<uintptr_t>(&gateJoinReq),
                                              reinterpret_cast<uintptr_t>(&gateJoinAcc),
                                              reinterpret_cast<uintptr_t>(&sendWithTrailerStub)};
    if (!applyCodePatches(version_gate_patches::patches(thunks))) {
        logger::write("version gate: sites differ, nothing installed; players are not told apart by version");
        return false;
    }
    logger::write("version gate: protocol version %u, max_players %u, partner_experimental %d", version::kProtocolVersion,
                  maxPlayers, partnerExperimental ? 1 : 0);
    return true;
}

}  // namespace version_gate

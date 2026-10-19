#include "version_gate.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "code_patch.h"
#include "guest_call.h"
#include "join_deny_rules.h"
#include "log.h"
#include "memory.h"
#include "trailer_rules.h"
#include "transport.h"
#include "version_gate_patches.h"

namespace {

using trailer_rules::Verdict;

constexpr size_t kMaxPayload = 1300;      // the game's message buffers
constexpr uintptr_t kPayloadLengthBack = 2;  // the u16 payload length sits just before the payload (3 byte header)
constexpr uintptr_t kContextMemberOffset = 4;   // ctx[1]: the member the sender is mapped to, 0 for a stranger
constexpr uintptr_t kSenderPortOffset = 4;      // in the sender address block (the IP is at 0)
constexpr size_t kAddressBlockWords = 5;        // {ip, port, 0, 0, 0}
constexpr size_t kDenyPayloadWords = 2;
constexpr size_t kRefusalChars = 160;           // the wide text shown for a refusal, its end mark included

trailer_rules::Settings g_settings;
bool g_trace = false;
std::mutex g_passwordMutex;
std::string g_joinPassword;  // what a join_req must carry, empty = nothing is checked
std::atomic<uint8_t> g_lastDenyReason{join_deny_rules::kReasonNone};
const uintptr_t g_send = version_gate_patches::kSend;
const uintptr_t g_errorText = version_gate_patches::kErrorText;

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
    const auto theirs = trailer_rules::read(bytes.data(), bytes.size());
    return {trailer_rules::compare(theirs, g_settings), theirs};
}

// The refusal the refused player's next join error popup names (VG1): set by the guest's gate and by a join_deny that ends
// in the host's trailer, shown once, forgotten when the next join_req is sent.
std::mutex g_pendingMutex;
std::optional<Inspection> g_pending;
wchar_t g_refusalText[kRefusalChars];

void setPending(const Inspection& inspection) {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pending = inspection.verdict == Verdict::Match ? std::nullopt : std::optional<Inspection>(inspection);
}

void clearPending() {
    std::lock_guard<std::mutex> lock(g_pendingMutex);
    g_pending.reset();
}

// The words of a log line that say why a message was refused.
void logRefusal(const char* who, uint32_t ip, uint16_t port, const Inspection& inspection) {
    const auto* octets = reinterpret_cast<const uint8_t*>(&ip);
    char why[192];
    if (inspection.theirs) {
        const trailer_rules::Trailer mine = trailer_rules::ours(g_settings);
        snprintf(why, sizeof(why),
                 "version %u players %u flags %u content 0x%016llX, ours version %u players %u flags %u content 0x%016llX",
                 inspection.theirs->protocolVersion, inspection.theirs->maxPlayers, inspection.theirs->flags,
                 static_cast<unsigned long long>(inspection.theirs->contentHash), mine.protocolVersion, mine.maxPlayers,
                 mine.flags, static_cast<unsigned long long>(mine.contentHash));
    } else {
        snprintf(why, sizeof(why), "no mod");
    }
    logger::write("version gate: %s %u.%u.%u.%u:%u refused, %s", who, octets[0], octets[1], octets[2], octets[3], port, why);
}

bool passwordAccepted(uintptr_t payload) {
    std::string secret;
    {
        std::lock_guard<std::mutex> lock(g_passwordMutex);
        secret = g_joinPassword;
    }
    if (secret.empty()) return true;
    uint8_t field[join_deny_rules::kMaxPasswordBytes] = {};
    return readMemory(payload, field, sizeof(field)) && join_deny_rules::passwordMatches(field, sizeof(field), secret);
}

// join_deny with `reason` and our trailer to the real sender; the sender's block is {ip, port, 0, 0, 0}. A game that builds
// join_deny with another header than the one built here sends it through its own function, without the trailer.
void denyJoin(uint32_t ip, uint32_t port, uint8_t reason) {
    uint32_t block[kAddressBlockWords] = {ip, port, 0, 0, 0};
    const uint32_t blockAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
    if (readByte(version_gate_patches::kJoinDenyHeaderFlag) != 0) {
        guest_call::call(version_gate_patches::kJoinDenySender, {.ecx = blockAddress}, {reason});
        return;
    }
    const auto trailer = trailer_rules::make(g_settings);
    const join_deny_rules::DenyMessage message = join_deny_rules::denyMessage(reason, trailer.data(), trailer.size());
    guest_call::call(version_gate_patches::kSendToAddress,
                     {.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(message.bytes.data()))},
                     {blockAddress, static_cast<uint32_t>(message.length)});
}

// G: host. A join_req without our trailer gets join_deny reason 1, one with the wrong password reason 7, both to the real
// sender and ending in our trailer, and nothing else happens.
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
        if (!passwordAccepted(payload)) {
            logger::write("version gate: join_req from 0x%08X:%u refused, wrong password", ip, port);
            denyJoin(ip, port, join_deny_rules::kReasonBadPassword);
            return;
        }
        if (g_trace) logger::write("version gate: join_req from 0x%08X:%u accepted", ip, port);
        callHandler(version_gate_patches::kJoinReqHandler, payload, ctx);
        return;
    }
    logRefusal("join_req from", ip, static_cast<uint16_t>(port), inspection);
    denyJoin(ip, port, join_deny_rules::kReasonWrongVersion);
}

// G: guest. Every join_deny is noted (the force join guest gives up on a refusal that no retry changes) and, when it ends
// in the host's trailer, compared with ours so the popup can name the difference; then the game handles it as it always
// did. A deny without a trailer (a vanilla host that is full or wants a password) names nothing.
void __cdecl noteJoinDeny(uintptr_t payload, uintptr_t ctx) {
    uint8_t reason = join_deny_rules::kReasonNone;
    readMemory(payload + join_deny_rules::kReasonOffset, &reason, sizeof(reason));
    g_lastDenyReason = reason;
    const Inspection host = inspect(payload);
    if (host.theirs) setPending(host);
    callHandler(version_gate_patches::kJoinDenyHandler, payload, ctx);
}

// G: guest. A join_acc without our trailer is refused through the game's own join_deny handler, and the popup names why.
void __cdecl gateJoinAcc(uintptr_t payload, uintptr_t ctx) {
    const Inspection inspection = inspect(payload);
    if (inspection.verdict == Verdict::Match) {
        if (g_trace) logger::write("version gate: join_acc accepted");
        callHandler(version_gate_patches::kJoinAccHandler, payload, ctx);
        return;
    }
    const uintptr_t sender = readDword(ctx);
    logRefusal("join_acc from host", readDword(sender), readWord(sender + kSenderPortOffset), inspection);
    setPending(inspection);
    g_lastDenyReason = join_deny_rules::kReasonWrongVersion;
    uint32_t deny[kDenyPayloadWords] = {join_deny_rules::kGameVersion, join_deny_rules::kReasonWrongVersion};
    callHandler(version_gate_patches::kJoinDenyHandler, reinterpret_cast<uintptr_t>(deny), ctx);
}

// V3, V4: `args` are the stack arguments of the game's send, (buffer, length, 0, 0). The trailer goes behind the message
// and both lengths grow; a message that would not fit goes out as it is (and is refused by the other side). A new join
// attempt forgets the refusal of the last one.
void __cdecl appendTrailer(uint32_t* args) {
    clearPending();
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

// VG1: the text of a join error code. Code 8 (join rejected) with a pending refusal gets its named reason, once; any other
// code gets null, which leaves the game's own text.
const wchar_t* __cdecl refusalTextFor(uint32_t code) {
    if (code != version_gate_patches::kJoinRejectedCode) return nullptr;
    std::optional<Inspection> refusal;
    {
        std::lock_guard<std::mutex> lock(g_pendingMutex);
        refusal.swap(g_pending);
    }
    if (!refusal) return nullptr;
    const std::string text = trailer_rules::refusalText(refusal->verdict, refusal->theirs, trailer_rules::ours(g_settings));
    size_t length = 0;
    for (; length < text.size() && length + 1 < kRefusalChars; ++length) {
        g_refusalText[length] = static_cast<unsigned char>(text[length]);
    }
    g_refusalText[length] = 0;
    return g_refusalText;
}

// Stands in for the call of the game's error text function in the join error popup (the code is in ecx): the named
// reason when there is one, else the game's own function.
void __declspec(naked) refusalTextStub() {
    __asm {
        push ecx
        call refusalTextFor
        pop ecx
        test eax, eax
        jnz named
        jmp dword ptr [g_errorText]
    named:
        ret
    }
}

}  // namespace

namespace version_gate {

bool install(uint32_t maxPlayers, bool steamTransport, uint64_t contentHash, bool trace) {
    g_trace = trace;
    g_settings = {static_cast<uint8_t>(maxPlayers), steamTransport, contentHash};
    const version_gate_patches::Thunks thunks{reinterpret_cast<uintptr_t>(&gateJoinReq),
                                              reinterpret_cast<uintptr_t>(&gateJoinAcc),
                                              reinterpret_cast<uintptr_t>(&sendWithTrailerStub),
                                              reinterpret_cast<uintptr_t>(&noteJoinDeny),
                                              reinterpret_cast<uintptr_t>(&refusalTextStub)};
    if (!applyCodePatches(version_gate_patches::patches(thunks))) {
        logger::write("version gate: sites differ, nothing installed; players are not told apart by version");
        return false;
    }
    logger::write("version gate: protocol version %u, max_players %u, transport %s, browser tag %s", version::kProtocolVersion,
                  maxPlayers, transportName(steamTransport ? Transport::Steam : Transport::Native), browserTag().c_str());
    return true;
}

std::string browserTag() { return trailer_rules::tagFor(trailer_rules::ours(g_settings)); }

void setJoinPassword(const std::string& password) {
    std::lock_guard<std::mutex> lock(g_passwordMutex);
    g_joinPassword = password;
}

uint8_t lastJoinDeny() { return g_lastDenyReason; }

void clearJoinDeny() { g_lastDenyReason = join_deny_rules::kReasonNone; }

}  // namespace version_gate

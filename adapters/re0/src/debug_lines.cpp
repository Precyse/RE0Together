#include "debug_lines.h"

#include <cstdarg>
#include <cstdio>

namespace {

using debug_stats::Counter;
using debug_stats::Gauge;
using debug_stats::Snapshot;

constexpr size_t kFormatCapacity = 128;
constexpr size_t kPeerNameChars = 14;
constexpr uint64_t kMsPerSecond = 1000;

std::wstring format(const wchar_t* fmt, ...) {
    wchar_t text[kFormatCapacity];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(text, _TRUNCATE, fmt, args);
    va_end(args);
    return text;
}

std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

uint32_t total(const Snapshot& s, Counter c) { return s.total[static_cast<size_t>(c)]; }
uint32_t rate(const Snapshot& s, Counter c) { return s.perSecond[static_cast<size_t>(c)]; }
int gauge(const Snapshot& s, Gauge g) { return s.gauge[static_cast<size_t>(g)]; }

std::wstring owner(int slot) {
    return slot == debug_stats::kUnknownOwner ? L"unknown" : format(L"slot %d", slot);
}

std::wstring partyMode(int mode) { return mode ? L"leave behind" : L"team"; }

std::wstring focused(int character) {
    if (character == debug_stats::kUnknownOwner) return L"none";
    return character ? L"rebecca" : L"billy";
}

std::wstring joined(const std::vector<std::string>& names) {
    std::wstring out;
    for (const std::string& name : names) out += (out.empty() ? L"" : L",") + widen(name);
    return out;
}

std::wstring ageText(const debug_stats::Note& note) {
    if (note.text.empty()) return L"none";
    return format(L"%llus ago: %ls", note.ageMs / kMsPerSecond, widen(note.text).c_str());
}

void addPeers(const Snapshot& s, std::vector<debug_lines::Line>& lines) {
    if (s.peers.empty()) lines.push_back({L"peers", L"0", false});
    for (const PeerInfo& peer : s.peers) {
        lines.push_back({format(L"peer %u", peer.slot),
                         format(L"%ls %u ms", widen(peer.name.substr(0, kPeerNameChars)).c_str(), peer.rttMs), false});
    }
}

}  // namespace

namespace debug_lines {

std::vector<Line> build(const Snapshot& s) {
    std::vector<Line> lines;
    lines.push_back({L"link", s.linked ? L"up" : L"down", !s.linked});
    lines.push_back({L"local slot", format(L"%u", s.localSlot), false});
    lines.push_back({L"host slot", format(L"%u", s.hostSlot), false});
    lines.push_back({L"epoch", format(L"%u", s.epoch), false});
    addPeers(s, lines);
    lines.push_back({L"billy owner", owner(gauge(s, Gauge::BillyOwner)), false});
    lines.push_back({L"rebecca owner", owner(gauge(s, Gauge::RebeccaOwner)), false});
    lines.push_back({L"door phase", format(L"%d", gauge(s, Gauge::DoorPhase)), false});
    lines.push_back({L"room", format(L"0x%04x", gauge(s, Gauge::Room)), false});
    lines.push_back({L"party mode", partyMode(gauge(s, Gauge::PartyMode)), false});
    lines.push_back({L"focused", focused(gauge(s, Gauge::FocusedCharacter)), false});
    lines.push_back({L"commands sent", format(L"%u", total(s, Counter::CommandsSent)), false});
    lines.push_back({L"last command", ageText(s.lastCommand), false});
    lines.push_back({L"last decision", ageText(s.lastDecision), false});
    lines.push_back({L"pickups aborted", format(L"%u", total(s, Counter::PickupsAborted)), false});
    lines.push_back({L"save redirect", gauge(s, Gauge::SaveRedirect) ? L"active" : L"off", false});
    lines.push_back({L"save reads", format(L"%u", total(s, Counter::SaveReads)), false});
    lines.push_back({L"save writes", format(L"%u", total(s, Counter::SaveWrites)), false});
    lines.push_back({L"world frozen", gauge(s, Gauge::WorldFrozen) ? L"yes (partner menu)" : L"no", false});
    lines.push_back({L"menu freezes", format(L"%u", total(s, Counter::MenuFreezes)), false});
    lines.push_back({L"inventory sent", format(L"%u", total(s, Counter::InventorySent)), false});
    lines.push_back({L"inventory applied", format(L"%u", total(s, Counter::InventoryApplied)), false});
    lines.push_back({L"floor put sent/applied", format(L"%u / %u", total(s, Counter::FloorPutSent), total(s, Counter::FloorPutApplied)), false});
    lines.push_back({L"floor take sent/applied", format(L"%u / %u", total(s, Counter::FloorTakeSent), total(s, Counter::FloorTakeApplied)), false});
    lines.push_back({L"floor pending", format(L"%d", gauge(s, Gauge::FloorPending)), false});
    lines.push_back({L"floor pend appl/drop", format(L"%u / %u", total(s, Counter::FloorPendingApplied), total(s, Counter::FloorPendingDropped)), false});
    lines.push_back({L"floor take misses", format(L"%u", total(s, Counter::FloorTakeMisses)), false});
    lines.push_back({L"room desyncs", format(L"%u", total(s, Counter::RoomDesyncs)), false});
    lines.push_back({L"doors sent/run/blocked", format(L"%u / %u / %u", total(s, Counter::DoorsSent), total(s, Counter::DoorsApplied), total(s, Counter::DoorsSuppressed)), false});
    lines.push_back({L"pad sent/s", format(L"%u", rate(s, Counter::PadSent)), false});
    lines.push_back({L"pad received/s", format(L"%u", rate(s, Counter::PadReceived)), false});
    lines.push_back({L"pad buffer", format(L"%d", gauge(s, Gauge::PadDepth)), false});
    lines.push_back({L"pad underruns", format(L"%u", total(s, Counter::PadUnderruns)), false});
    lines.push_back({L"pad skips", format(L"%u", total(s, Counter::PadSkips)), false});
    lines.push_back({L"state sent/s", format(L"%u", rate(s, Counter::PlayerStateSent)), false});
    lines.push_back({L"state received/s", format(L"%u", rate(s, Counter::PlayerStateReceived)), false});
    lines.push_back({L"blends/s", format(L"%u", rate(s, Counter::Blends)), false});
    lines.push_back({L"snaps", format(L"%u", total(s, Counter::Snaps)), false});
    lines.push_back({L"hit request sent/s", format(L"%u", rate(s, Counter::HitRequestSent)), false});
    lines.push_back({L"hit request recv/s", format(L"%u", rate(s, Counter::HitRequestReceived)), false});
    lines.push_back({L"hit applied sent/s", format(L"%u", rate(s, Counter::HitAppliedSent)), false});
    lines.push_back({L"hit applied recv/s", format(L"%u", rate(s, Counter::HitAppliedReceived)), false});
    lines.push_back({L"enemy sent/s", format(L"%u", rate(s, Counter::EnemyStateSent)), false});
    lines.push_back({L"enemy received/s", format(L"%u", rate(s, Counter::EnemyStateReceived)), false});
    lines.push_back({L"enemy mismatches", format(L"%u", total(s, Counter::EnemyMismatches)), false});
    lines.push_back({L"deaths reported", format(L"%u", total(s, Counter::DeathsReported)), false});
    lines.push_back({L"deaths applied", format(L"%u", total(s, Counter::DeathsApplied)), false});
    lines.push_back({L"disabled", s.disabledCallbacks.empty() ? L"none" : joined(s.disabledCallbacks),
                     !s.disabledCallbacks.empty()});
    const bool hasError = !s.lastError.empty();
    lines.push_back({L"error", hasError ? format(L"%llus %ls", s.lastErrorAgeMs / kMsPerSecond, widen(s.lastError).c_str())
                                        : L"none",
                     hasError});
    for (Line& line : lines) {
        if (line.value.size() > kValueChars) line.value.resize(kValueChars);
    }
    return lines;
}

}  // namespace debug_lines

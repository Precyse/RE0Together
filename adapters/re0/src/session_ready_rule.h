#pragma once
#include <cstdint>
#include <optional>

#include "peer_sight.h"
#include "session_wire.h"

// Rules of the session-ready handshake (partner_mode=state, docs/RE0_NOTES.md "State mode: session"). After a join or a
// resync the guest tells the host which room and load it stands in; the host accepts when the guest's own newest pose says
// the same and is fresh, and echoes it. Pure, no game or network access, unit tested.
namespace session_ready_rule {

using session_wire::SessionReady;

constexpr uint32_t kResendMs = 1000;  // the guest asks again this often until the echo comes

enum class Verdict : uint8_t { Accept, UnknownSnapshot, NoPose, StalePose, OtherRoom, OtherLoad };

constexpr const char* name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Accept: return "accepted";
        case Verdict::UnknownSnapshot: return "unknown snapshot";
        case Verdict::NoPose: return "no pose";
        case Verdict::StalePose: return "stale pose";
        case Verdict::OtherRoom: return "other room";
        case Verdict::OtherLoad: return "other load";
    }
    return "unknown";
}

// Host: `latestSerial` is the serial of the newest JOIN_EXTRAS this host sent; a ready that names a snapshot the host never
// made is refused. `pose` is the newest pose of the guest's own stream.
constexpr Verdict judge(const SessionReady& ready, const std::optional<peer_sight::Sight>& pose, uint32_t latestSerial,
                        uint32_t nowMs) {
    if (ready.snapshotSerial > latestSerial) return Verdict::UnknownSnapshot;
    if (!pose) return Verdict::NoPose;
    if (!peer_sight::fresh(*pose, nowMs)) return Verdict::StalePose;
    if (pose->room != ready.scene) return Verdict::OtherRoom;
    if (pose->loadSerial != ready.loadSerial) return Verdict::OtherLoad;
    return Verdict::Accept;
}

// The echo of an accepted ready: the same values, the Echo phase.
constexpr SessionReady echoOf(const SessionReady& ready) {
    SessionReady echo = ready;
    echo.phase = static_cast<uint8_t>(session_wire::ReadyPhase::Echo);
    return echo;
}

// Guest: when to send SESSION_READY and whether an echo answers it. A handshake starts when a snapshot with its extras is
// applied (`arm`); the ready is sent at once, again when the room or load changes before the echo, and every kResendMs
// until the echo comes. `echoed` stays true from the first echo until `reset` (a resync starts a new handshake but does not
// hold back the guest's own state).
class ReadyClient {
public:
    void arm(uint32_t snapshotSerial) {
        serial_ = snapshotSerial;
        armed_ = true;
        sent_ = false;
    }

    std::optional<SessionReady> next(uint32_t nowMs, uint16_t scene, uint16_t loadSerial) {
        if (!armed_ || scene >= session_wire::kSceneLimit || loadSerial == 0) return std::nullopt;
        const bool same = sent_ && last_.scene == scene && last_.loadSerial == loadSerial;
        if (same && nowMs - sentMs_ < kResendMs) return std::nullopt;
        last_ = {static_cast<uint8_t>(session_wire::ReadyPhase::Ready), 0, scene, loadSerial, 0, serial_};
        sent_ = true;
        sentMs_ = nowMs;
        return last_;
    }

    // True when `echo` repeats the ready that was sent last.
    bool onEcho(const SessionReady& echo) {
        if (!armed_ || !sent_ || echo.phase != static_cast<uint8_t>(session_wire::ReadyPhase::Echo) ||
            echo.scene != last_.scene || echo.loadSerial != last_.loadSerial || echo.snapshotSerial != last_.snapshotSerial) {
            return false;
        }
        armed_ = false;
        echoed_ = true;
        return true;
    }

    bool echoed() const { return echoed_; }
    bool armed() const { return armed_; }

    void reset() { *this = ReadyClient{}; }

private:
    bool armed_ = false;
    bool sent_ = false;
    bool echoed_ = false;
    uint32_t serial_ = 0;
    uint32_t sentMs_ = 0;
    SessionReady last_{};
};

}  // namespace session_ready_rule

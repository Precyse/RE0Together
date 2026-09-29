#pragma once

// Pure decision logic of the party commands (no game, unit tested).
namespace command_rule {

// Edge detector of one key, polled every tick. `reset` returns it to a known state that cannot fire a phantom
// press: a key still held after a reset stays quiet until it is released.
struct EdgeDetector {
    bool wasDown = false;

    bool update(bool down) {
        const bool edge = down && !wasDown;
        wasDown = down;
        return edge;
    }

    void reset() { wasDown = true; }
};

enum class PressVerdict { Send, NotForeground, NoPeer };

// A detected local key press becomes a request only with the game window in front and a peer connected.
constexpr PressVerdict decidePress(bool foreground, bool peerActive) {
    if (!foreground) return PressVerdict::NotForeground;
    return peerActive ? PressVerdict::Send : PressVerdict::NoPeer;
}

struct SwitchState {
    bool peerActive;
    bool hasControlled;
    bool hasPartner;
    bool doorActive;
    bool focusedIsTarget;  // the controlled character already is the requested one
    bool partnerIsTarget;  // the partner is the requested one
};

enum class SwitchVerdict { Apply, NoPeer, NoPartner, DoorActive, AlreadyFocused, PartnerIsOther };

// Whether the host can apply a switch request now, or why not.
constexpr SwitchVerdict decideSwitch(const SwitchState& s) {
    if (!s.peerActive) return SwitchVerdict::NoPeer;
    if (!s.hasControlled || !s.hasPartner) return SwitchVerdict::NoPartner;
    if (s.doorActive) return SwitchVerdict::DoorActive;
    if (s.focusedIsTarget) return SwitchVerdict::AlreadyFocused;
    return s.partnerIsTarget ? SwitchVerdict::Apply : SwitchVerdict::PartnerIsOther;
}

// True when the players cannot be touched at all (as opposed to a request that just has nothing to do).
constexpr bool blocksHost(SwitchVerdict verdict) {
    return verdict == SwitchVerdict::NoPeer || verdict == SwitchVerdict::NoPartner ||
           verdict == SwitchVerdict::DoorActive;
}

constexpr const char* reason(SwitchVerdict verdict) {
    switch (verdict) {
        case SwitchVerdict::NoPeer: return "no peer connected";
        case SwitchVerdict::NoPartner: return "no partner object";
        case SwitchVerdict::DoorActive: return "door active";
        case SwitchVerdict::AlreadyFocused: return "focus already set";
        case SwitchVerdict::PartnerIsOther: return "partner is not the target";
        case SwitchVerdict::Apply: break;
    }
    return "applied";
}

}  // namespace command_rule

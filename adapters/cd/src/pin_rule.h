#pragma once
// The other player as a map pin while their copy is not on this screen (pure, unit tested; cd design players.md 7,
// STUDY.md "Minimap icon and map pin"). The pin is the engine's CreateOrChangePinMarkerAck / RemovePinMarkerAck,
// injected into our own client only: it never reaches the server or the save. The owner must be the LOCAL player,
// the group 0 or 1 (2+ writes past the client's two lists), shape < 14, colour < 15. Re-sending an id moves the icon
// but not the stored position, so a move is Remove + Create.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace pin_rule {

constexpr uint64_t kPinIdBase = 0x7FDD00000000F001ull;  // far outside the server's ids (it hands them out from 0)
constexpr uint8_t kGroupNormal = 0;
constexpr uint8_t kShape = 4;    // the game's default pin
constexpr uint8_t kColour = 14;  // the game's default pin colour
constexpr uint8_t kCreateMarker = 4;
constexpr size_t kCreateHeader = 6;   // marker, u32 owner, u8 group
constexpr size_t kCreateEntry = 22;   // u64 id, f32 xyz, u8 shape, u8 colour
constexpr size_t kRemoveSize = 13;    // u32 owner, u64 id, u8 group

constexpr float kMoveM = 3.0f;          // moved this far: the pin follows
constexpr uint64_t kMoveEveryMs = 1000; // but at most this often
constexpr uint64_t kRefreshMs = 10000;  // re-created this often even when still

inline uint64_t pinId(uint8_t slot) { return kPinIdBase + slot; }

struct Pin {
    bool shown = false;
    float at[3] = {};
    uint64_t createdMs = 0;
};

struct Step {
    bool remove = false;
    bool create = false;
};

// `where`: the peer's position when a pin is wanted (their copy is not here, both worlds Ready in the same field),
// otherwise null. Updates `pin` to what the step leaves on screen.
inline Step decide(Pin& pin, const std::optional<std::array<float, 3>>& where, uint64_t nowMs) {
    Step step;
    if (!where) {
        step.remove = pin.shown;
        pin.shown = false;
        return step;
    }
    if (pin.shown) {
        const float dx = (*where)[0] - pin.at[0], dy = (*where)[1] - pin.at[1], dz = (*where)[2] - pin.at[2];
        const bool moved = std::sqrt(dx * dx + dy * dy + dz * dz) >= kMoveM && nowMs - pin.createdMs >= kMoveEveryMs;
        if (!moved && nowMs - pin.createdMs < kRefreshMs) return step;
        step.remove = true;
    }
    step.create = true;
    pin.shown = true;
    std::memcpy(pin.at, where->data(), sizeof(pin.at));
    pin.createdMs = nowMs;
    return step;
}

inline std::vector<uint8_t> createPayload(uint32_t owner, uint64_t id, const float at[3]) {
    std::vector<uint8_t> out(kCreateHeader + kCreateEntry);
    out[0] = kCreateMarker;
    std::memcpy(out.data() + 1, &owner, sizeof(owner));
    out[5] = kGroupNormal;
    std::memcpy(out.data() + kCreateHeader, &id, sizeof(id));
    std::memcpy(out.data() + kCreateHeader + sizeof(id), at, 3 * sizeof(float));
    out[kCreateHeader + 20] = kShape;
    out[kCreateHeader + 21] = kColour;
    return out;
}

inline std::vector<uint8_t> removePayload(uint32_t owner, uint64_t id) {
    std::vector<uint8_t> out(kRemoveSize);
    std::memcpy(out.data(), &owner, sizeof(owner));
    std::memcpy(out.data() + 4, &id, sizeof(id));
    out[12] = kGroupNormal;
    return out;
}

}  // namespace pin_rule

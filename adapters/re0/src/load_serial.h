#pragma once
#include <cstdint>

// The load serial: a counter of the loaded room record that changes whenever the room is loaded again, so a stream
// stamped with it can be reset even when the scene id alone repeats (left a room and came back to it). Pure, no game
// access; fed once per game tick by scene::observeLoad.
namespace load_serial {

constexpr uint16_t kFirstSerial = 1;  // 0 is never a serial: it means "not stamped"

class Tracker {
public:
    // Feeds the loaded room record (0 while none is loaded: a load in progress) and its scene id. A new record, or a
    // new scene id in the same record, advances the serial; so does the first record after none. True when it advanced.
    bool observe(uintptr_t record, uint16_t sceneId) {
        if (record == 0) {
            record_ = 0;
            return false;
        }
        if (record == record_ && sceneId == sceneId_) return false;
        record_ = record;
        sceneId_ = sceneId;
        serial_ = serial_ == UINT16_MAX ? kFirstSerial : static_cast<uint16_t>(serial_ + 1);
        return true;
    }

    uint16_t value() const { return serial_; }

private:
    uintptr_t record_ = 0;
    uint16_t sceneId_ = 0;
    uint16_t serial_ = kFirstSerial;
};

}  // namespace load_serial

#pragma once

// Pure rule (unit tested): the first tick of this machine's own door out of a room it runs while the other player is in
// the room. The boss hand-over prepares its members then and the enemy observers send their final pose round.
namespace door_leave_rule {

// One latch per user: the door is reported once, and the latch is released when the door ends.
class Departure {
public:
    // True on the first tick the door is active while `runsRoom` and `peerHere` hold.
    bool starts(bool doorActive, bool runsRoom, bool peerHere) {
        if (!doorActive) {
            latched_ = false;
            return false;
        }
        if (latched_ || !runsRoom || !peerHere) return false;
        latched_ = true;
        return true;
    }

private:
    bool latched_ = false;
};

}  // namespace door_leave_rule

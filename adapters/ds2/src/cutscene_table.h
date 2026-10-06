#pragma once
// The cutscenes this machine holds, plays or waits for, and the rules that decide whether the engine's Sequence start is let
// through (ds2/cutscene.cpp feeds it the engine's calls; no engine access here, so tests run it).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cutscene_wire.h"
#include "ds2/sequence_info.h"

namespace cutscene_table {

constexpr uint64_t kHoldLimitMs = 7000;  // the longest a cutscene is held (counted from the hold, not the announcement), whatever went wrong
constexpr uint64_t kUnannouncedHoldLimitMs = 7000;  // a guest's own shared Sequence the host never announces is held this long, then plays
constexpr size_t kMaxPlaybacks = 32;

enum class Phase { Announced, Held, Released, Playing };

struct Playback {
    cutscene_wire::Start start;
    Phase phase = Phase::Announced;
    uintptr_t sequence = 0;
    uint8_t ownEntity[cutscene_wire::kUuidSize] = {};  // this machine's Sequence entity (a guest's differs from the host's)
    uint64_t createdMs = 0;
    uint64_t heldSinceMs = 0;  // when the Sequence was held here: the hold limit counts from it
    uint64_t releaseAtMs = 0;
    uint64_t networkStartedMs = 0;
    bool goEarly = false;  // guest: GO arrived before the Sequence was held
    bool adoptTried = false;
};

struct Orphan {  // guest: a shared Sequence its own graph started before the host announced it
    uintptr_t sequence;
    sequence_info::Info info;
    uint64_t sinceMs;
    bool released = false;  // held past the limit: it plays, and keeps passing
};

struct Verdict {
    bool hold = false;
    uint32_t id = 0;       // the playback concerned, 0 when none
    bool created = false;  // host: a new hold began (START queued); guest: an unannounced Sequence was held
    bool forced = false;   // the hold limit let it start
};

inline bool sameUuid(const uint8_t* a, const uint8_t* b) { return std::memcmp(a, b, cutscene_wire::kUuidSize) == 0; }

class Table {
public:
    std::vector<Playback> playbacks;
    std::vector<Orphan> orphans;
    std::vector<cutscene_wire::Start> outStarts;
    std::vector<cutscene_wire::End> outEnds;
    std::vector<uint32_t> outReady;
    std::vector<cutscene_wire::End> inEnds;

    Playback* bySequence(uintptr_t sequence) {
        for (Playback& p : playbacks) {
            if (p.sequence == sequence) return &p;
        }
        return nullptr;
    }

    Playback* byId(uint32_t id) {
        for (Playback& p : playbacks) {
            if (p.start.id == id) return &p;
        }
        return nullptr;
    }

    Playback* waitingFor(const sequence_info::Info& info) {
        for (Playback& p : playbacks) {
            if (p.phase == Phase::Announced && sameUuid(p.start.resource, info.resource)) return &p;
        }
        return nullptr;
    }

    void erase(const Playback* p) { playbacks.erase(playbacks.begin() + (p - playbacks.data())); }

    bool playing() const {
        return std::any_of(playbacks.begin(), playbacks.end(), [](const Playback& p) { return p.phase == Phase::Playing; });
    }

    // Host: the engine starts `sequence`. Holds it, announcing it, while guests exist.
    Verdict hostDecide(uintptr_t sequence, const sequence_info::Info& info, uint64_t now, size_t guests) {
        Playback* p = bySequence(sequence);
        if (!p) {
            if (guests == 0 || playbacks.size() >= kMaxPlaybacks) return {};
            Playback fresh;
            fresh.start = makeStart(++nextId_, info);
            fresh.phase = Phase::Held;
            fresh.sequence = sequence;
            std::memcpy(fresh.ownEntity, info.entity, sizeof(fresh.ownEntity));
            fresh.createdMs = now;
            fresh.heldSinceMs = now;
            playbacks.push_back(fresh);
            outStarts.push_back(fresh.start);
            return {true, fresh.start.id, true, false};
        }
        return passOrHold(*p, now);
    }

    // Guest: the engine starts `sequence`. Holds it until the host's announcement matches it and the host says go.
    Verdict guestDecide(uintptr_t sequence, const sequence_info::Info& info, uint64_t now) {
        Playback* p = bySequence(sequence);
        if (!p) p = waitingFor(info);
        if (!p) return holdOrphan(sequence, info, now);
        if (p->phase == Phase::Announced) bind(*p, sequence, info, now);
        return passOrHold(*p, now);
    }

    // A Playback of the guest's that has no Sequence yet takes this one.
    void bind(Playback& p, uintptr_t sequence, const sequence_info::Info& info, uint64_t now) {
        p.sequence = sequence;
        std::memcpy(p.ownEntity, info.entity, sizeof(p.ownEntity));
        p.phase = Phase::Held;
        p.heldSinceMs = now;
        outReady.push_back(p.start.id);
        if (p.goEarly) {
            p.phase = Phase::Released;
            p.releaseAtMs = now;
        }
    }

    // Guest: this machine cannot play the announced cutscene (its Sequence is not loaded here, e.g. the guest is far away):
    // the host is told ready at once so it does not wait for the timeout, and the playback is forgotten.
    void giveUp(uint32_t id) {
        Playback* p = byId(id);
        if (!p || p->phase != Phase::Announced) return;
        outReady.push_back(id);
        erase(p);
    }

    // Host: every guest is ready; the cutscene may start `delayMs` from now.
    void release(uint32_t id, uint64_t now, uint32_t delayMs) {
        Playback* p = byId(id);
        if (!p || p->phase != Phase::Held) return;
        p->phase = Phase::Released;
        p->releaseAtMs = now + delayMs;
    }

    // Guest: the host's go.
    void go(uint32_t id, uint64_t now) {
        Playback* p = byId(id);
        if (!p) return;
        if (p->phase == Phase::Held) {
            p->phase = Phase::Released;
            p->releaseAtMs = now;
        } else if (p->phase == Phase::Announced) {
            p->goEarly = true;
        }
    }

    // Guest: the host's announcement.
    void arm(const cutscene_wire::Start& start, uint64_t now) {
        if (byId(start.id) || playbacks.size() >= kMaxPlaybacks) return;
        Playback p;
        p.start = start;
        p.createdMs = now;
        playbacks.push_back(p);
    }

private:
    // A shared Sequence nobody announced: held, then let through after the limit so a guest's own trigger cannot soft-lock.
    Verdict holdOrphan(uintptr_t sequence, const sequence_info::Info& info, uint64_t now) {
        auto it = std::find_if(orphans.begin(), orphans.end(), [&](const Orphan& o) { return o.sequence == sequence; });
        if (it == orphans.end()) {
            if (orphans.size() >= kMaxPlaybacks) return {true, 0, false, false};
            orphans.push_back({sequence, info, now});
            return {true, 0, true, false};
        }
        if (it->released) return {};
        if (now - it->sinceMs > kUnannouncedHoldLimitMs) {
            it->released = true;
            return {false, 0, false, true};
        }
        return {true, 0, false, false};
    }

    // Released and due: it plays. Held longer than the limit: it plays too, a cutscene must not hold the game for good.
    Verdict passOrHold(Playback& p, uint64_t now) {
        if (p.phase == Phase::Released && now >= p.releaseAtMs) {
            p.phase = Phase::Playing;
            return {false, p.start.id, false, false};
        }
        const bool held = p.phase == Phase::Held || p.phase == Phase::Released;
        if (held && now - p.heldSinceMs > kHoldLimitMs) {
            p.phase = Phase::Playing;
            return {false, p.start.id, false, true};
        }
        return {p.phase != Phase::Playing, p.start.id, false, false};
    }

    static cutscene_wire::Start makeStart(uint32_t id, const sequence_info::Info& info) {
        cutscene_wire::Start start{};
        start.id = id;
        start.category = info.category;
        start.stopFrame = info.stopFrame;
        std::memcpy(start.resource, info.resource, sizeof(start.resource));
        std::memcpy(start.entity, info.entity, sizeof(start.entity));
        std::memcpy(start.network, info.network, sizeof(start.network));
        return start;
    }

    uint32_t nextId_ = 0;
};

}  // namespace cutscene_table

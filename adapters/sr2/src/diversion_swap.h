#pragma once
#include "diversion_remotes.h"
#include "diversion_rules.h"
#include "partner_scope.h"

// Runs the game's code for one remote when the game keeps a single field for "the remote": the streaking coat, the Snatch
// waypoint of the remote side. Alive, the getters answer that remote and the game's field holds that remote's value; the
// default partner's value waits in the table meanwhile (docs/SR2_NOTES.md, "Diversions and activities", swap rule).
namespace diversion_swap {

// `Access` reads and writes the game's field as a T: `T read() const`, `void write(const T&) const`.
template <typename T, typename Access>
class RemoteTurn {
public:
    RemoteTurn(diversion_rules::SwapTable<T>& table, const Access& access, const diversion_remotes::View& view, size_t index)
        : table_(table), access_(access), remote_(view.items()[index]), override_({remote_.human, remote_.member}) {
        T cell = access_.read();
        if (view.defaultIndex < view.count()) table_.follow(view.items()[view.defaultIndex].slot, view.defaultHuman(), cell);
        entered_ = !table_.isCanonical(remote_.slot, remote_.human);
        if (entered_) table_.enter(remote_.slot, remote_.human, cell);
        access_.write(cell);
    }

    ~RemoteTurn() {
        if (!entered_) return;
        T cell = access_.read();
        table_.leave(remote_.slot, remote_.human, cell);
        access_.write(cell);
    }

    RemoteTurn(const RemoteTurn&) = delete;
    RemoteTurn& operator=(const RemoteTurn&) = delete;

private:
    diversion_rules::SwapTable<T>& table_;
    const Access& access_;
    partner_rules::Remote remote_;
    partner_scope::Override override_;
    bool entered_ = false;
};

}  // namespace diversion_swap

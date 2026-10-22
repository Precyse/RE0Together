#pragma once
#include <initializer_list>

#include "partner_sites.h"
#include "partner_thunks.h"

// The code behind the sites of diversion_sites.h (milestone 16 wave 2), split by area: diversion_select.cpp (which remote a
// test is about), diversion_each.cpp (calls and loops that run once per remote), diversion_fraud.cpp (zones, team hits and
// tandem bonuses), diversion_snatch.cpp (proximity, carry counts and the phase B spawn and waypoints) and diversion_streak.cpp
// (the streaking coat).
namespace diversion_thunks {

// Each returns {nullptr, nullptr} for a thunk that belongs to another file.
partner_thunks::Binding bindSelect(partner_sites::Thunk thunk);
partner_thunks::Binding bindEach(partner_sites::Thunk thunk);
partner_thunks::Binding bindFraud(partner_sites::Thunk thunk);
partner_thunks::Binding bindSnatch(partner_sites::Thunk thunk);
partner_thunks::Binding bindStreak(partner_sites::Thunk thunk);

// The binding of a thunk of any of the five files.
inline partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    for (const auto bindOne : {&bindSelect, &bindEach, &bindFraud, &bindSnatch, &bindStreak}) {
        const partner_thunks::Binding binding = bindOne(thunk);
        if (binding.entry) return binding;
    }
    return {nullptr, nullptr};
}

}  // namespace diversion_thunks

#pragma once
// The shared "applying a partner's state" flag. While the adapter writes the partner's state into the local world (a
// fact, a piece of cargo, an animation variable) its own hooks must not report that write back as a local change.
// Hold a `remote_apply::Scope` around the engine call; every hook asks `remote_apply::active()` first. The flag is per
// thread, because the engine call and the hook it triggers run on the same thread, and scopes nest.
namespace remote_apply {

inline int& depth() {
    thread_local int value = 0;
    return value;
}

inline bool active() { return depth() > 0; }

class Scope {
public:
    Scope() { ++depth(); }
    ~Scope() { --depth(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace remote_apply

#pragma once
// A set of engine object pointers the brain-off gates test from any thread (puppet bodies, enemy mirrors): written on
// the game thread, read under a shared lock, skipped without the lock while empty.
#include <windows.h>

#include <atomic>
#include <set>
#include <utility>

class ObjectSet {
public:
    void replace(std::set<const void*> objects) {
        AcquireSRWLockExclusive(&lock_);
        objects_ = std::move(objects);
        count_ = objects_.size();
        ReleaseSRWLockExclusive(&lock_);
    }

    bool contains(const void* object) const {
        if (!object || !count_.load(std::memory_order_relaxed)) return false;
        AcquireSRWLockShared(&lock_);
        const bool found = objects_.count(object) != 0;
        ReleaseSRWLockShared(&lock_);
        return found;
    }

private:
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::set<const void*> objects_;
    std::atomic<size_t> count_{0};
};

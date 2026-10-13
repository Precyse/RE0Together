#pragma once
#include <immintrin.h>

// Saves the whole x87 and SSE state on entry and restores it on exit. The game's code is x87 and its callers may
// keep floats on the FPU stack across a call that, in the shipped game, never touches the FPU; the thunks that do
// float math (distances) run under this guard.
class FpuGuard {
public:
    FpuGuard() { _fxsave(state_); }
    ~FpuGuard() { _fxrstor(state_); }
    FpuGuard(const FpuGuard&) = delete;
    FpuGuard& operator=(const FpuGuard&) = delete;

private:
    alignas(16) unsigned char state_[512];
};

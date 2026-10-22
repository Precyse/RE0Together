#include "session_tick.h"

#include "coop_autosave.h"
#include "peer_stall.h"
#include "thunk_frame.h"

namespace {

void* g_tickOriginal = nullptr;

void __cdecl sessionTick(GuestFrame*) {
    peer_stall::tick();
    coop_autosave::tick();
}

TRACE_ENTRY_STUB(sessionTickStub, sessionTick, g_tickOriginal)

}  // namespace

namespace session_tick {

partner_thunks::Binding bind(partner_sites::Thunk thunk) {
    switch (thunk) {
        case partner_sites::Thunk::SessionTick: return {reinterpret_cast<void*>(&sessionTickStub), &g_tickOriginal};
        default: return {nullptr, nullptr};
    }
}

}  // namespace session_tick

#include "init.h"

#include "code_check.h"
#include "config.h"
#include "coop_limits.h"
#include "hooks.h"
#include "join_trace.h"
#include "log.h"

DWORD WINAPI initThread(LPVOID) {
    try {
        logger::write("sr2 adapter starting");
        const Config config = loadConfig();
        logger::write("config: max_players=%u trace=%d", config.maxPlayers, config.trace ? 1 : 0);
        if (!gameMatchesBuild()) return 0;

        hooks::beginBatch();
        coop_limits::install(config.maxPlayers);
        if (config.trace) join_trace::install();
        hooks::applyBatch();
        logger::write("sr2 adapter ready");
    } catch (...) {
        logger::write("sr2 adapter: init failed with an exception");
    }
    return 0;
}

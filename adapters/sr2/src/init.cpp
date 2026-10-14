#include "init.h"

#include "code_check.h"
#include "config.h"
#include "coop_limits.h"
#include "game_port.h"
#include "game_port_rules.h"
#include "hooks.h"
#include "join_net_trace.h"
#include "join_trace.h"
#include "lan_address.h"
#include "log.h"
#include "partner_patches.h"
#include "player_cap.h"
#include "replication_switch.h"
#include "sr2_addresses.h"

DWORD WINAPI initThread(LPVOID) {
    try {
        logger::write("sr2 adapter starting");
        const Config config = loadConfig();
        logger::write("config: max_players=%u trace=%d partner_experimental=%d game_port=%u", config.maxPlayers,
                      config.trace ? 1 : 0, config.partnerExperimental ? 1 : 0, config.gamePort);
        if (config.rejectedGamePort != 0) {
            logger::write("config: game_port %u is outside %u..%u, ignored", config.rejectedGamePort,
                          game_port_rules::kMinGamePort, game_port_rules::kMaxGamePort);
        }
        const bool replicating = config.maxPlayers > sr2::kCoopDefaultPlayers;
        bool supported = gameMatchesBuild();
        if (replicating) supported = replication_switch::sitesMatch() && supported;
        if (!supported) return 0;
        const bool partnering = replicating && partner_patches::sitesMatch();
        if (replicating && !partnering) logger::write("partner: sites differ, the partner logic is not installed");

        hooks::beginBatch();
        if (replicating) {
            coop_limits::install(config.maxPlayers);
        } else {
            logger::write("replication: off, max_players %u is the shipped co-op, limits and pools are not patched",
                          config.maxPlayers);
        }
        player_cap::install(config.maxPlayers, config.trace);
        game_port::install(config.gamePort, config.maxPlayers, config.trace);
        lan_address::install(config.maxPlayers, config.trace);
        if (partnering) partner_patches::install(config.partnerExperimental, config.trace, config.maxPlayers);
        if (config.trace) {
            join_trace::install(config.maxPlayers);
            join_net_trace::install();
        }
        hooks::applyBatch();
        logger::write("sr2 adapter ready");
    } catch (...) {
        logger::write("sr2 adapter: init failed with an exception");
    }
    return 0;
}

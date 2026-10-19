#include "init.h"

#include "activity_patches.h"
#include "code_check.h"
#include "config.h"
#include "content_hash.h"
#include "coop_limits.h"
#include "force_join.h"
#include "game_port.h"
#include "game_port_rules.h"
#include "game_socket.h"
#include "hooks.h"
#include "join_net_trace.h"
#include "join_trace.h"
#include "lan_address.h"
#include "log.h"
#include "multiplayer_patches.h"
#include "outfit_sync.h"
#include "partner_patches.h"
#include "player_cap.h"
#include "player_name.h"
#include "player_visuals.h"
#include "replication_switch.h"
#include "script_override.h"
#include "sr2_addresses.h"
#include "tunnel.h"
#include "vehicle_pool.h"
#include "vehicle_pool_rules.h"
#include "version_gate.h"

DWORD WINAPI initThread(LPVOID) {
    try {
        logger::write("sr2 adapter starting");
        const Config config = loadConfig();
        logger::write("config: max_players=%u trace=%d partner_experimental=%d game_port=%u transport=%s", config.maxPlayers,
                      config.trace ? 1 : 0, config.partnerExperimental ? 1 : 0, config.gamePort,
                      transportName(config.transport));
        if (!config.rejectedTransport.empty()) {
            logger::write("config: transport \"%s\" is neither %s nor %s, ignored", config.rejectedTransport.c_str(),
                          kTransportNative, kTransportSteam);
        }
        if (config.rejectedGamePort != 0) {
            logger::write("config: game_port %u is outside %u..%u, ignored", config.rejectedGamePort,
                          game_port_rules::kMinGamePort, game_port_rules::kMaxGamePort);
        }
        const bool replicating = config.maxPlayers > sr2::kCoopDefaultPlayers;
        bool supported = gameMatchesBuild();
        if (replicating) supported = replication_switch::sitesMatch() && supported;
        if (!supported) return 0;
        if (replicating) player_visuals::installSkinPool(config.maxPlayers);
        const bool partnering = replicating && partner_patches::sitesMatch();
        if (replicating && !partnering) logger::write("partner: sites differ, the partner logic is not installed");
        const bool activities = partnering && activity_patches::sitesMatch();
        if (partnering && !activities) logger::write("activities: sites differ, the activity logic is not installed");
        const bool crowd = partnering && multiplayer_patches::sitesMatch();
        if (partnering && !crowd) logger::write("players 3 and 4: sites differ, that logic is not installed");
        if (vehicle_pool_rules::requested(config.maxPlayers, crowd)) vehicle_pool::request();

        const bool tunnelled = tunnel::install(config.transport, config.trace);
        const Transport transport = tunnelled ? Transport::Steam : Transport::Native;
        if (tunnelled && config.gamePort != sr2::kGamePort) {
            logger::write("config: game_port %u is ignored with the steam transport, every member is at %u", config.gamePort,
                          sr2::kGamePort);
        }
        game_socket::start();

        script_override::scan(config.trace);
        const uint64_t contentHash = content_hash::compute();
        version_gate::install(config.maxPlayers, config.partnerExperimental && partnering, tunnelled, contentHash,
                              config.trace);

        hooks::beginBatch();
        if (replicating) {
            coop_limits::install(config.maxPlayers);
        } else {
            logger::write("replication: off, max_players %u is the shipped co-op, limits and pools are not patched",
                          config.maxPlayers);
        }
        const bool playerCap = player_cap::install(config.maxPlayers, config.trace);
        game_port::install(config.gamePort, config.maxPlayers, config.trace, transport);
        if (!tunnelled) lan_address::install(config.maxPlayers, config.trace);
        force_join::install(config.trace, transport);
        player_name::install(config.trace);
        if (partnering) partner_patches::install(config.partnerExperimental, config.trace, config.maxPlayers);
        if (partnering) outfit_sync::install(config.trace);
        if (activities) activity_patches::install(config.partnerExperimental, config.trace);
        if (crowd) multiplayer_patches::install(config.trace, playerCap, config.partnerExperimental);

        if (config.trace) {
            join_trace::install(config.maxPlayers);
            join_net_trace::install();
            multiplayer_patches::installScriptTraces();
        }
        hooks::applyBatch();
        logger::write("sr2 adapter ready");
    } catch (...) {
        logger::write("sr2 adapter: init failed with an exception");
    }
    return 0;
}

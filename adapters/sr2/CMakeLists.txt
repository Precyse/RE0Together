cmake_minimum_required(VERSION 3.20)
project(coop_sr2_adapter C CXX)

# x86 only (Saints Row 2 is a 32-bit game). Build from a vcvars32 shell (rebuild.cmd).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# Shared with the RE0 adapter: logging, paths, the MinHook wrapper, the vendored MinHook and the dinput8 export list.
set(RE0_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../re0)
set(SHARED_DIR ${RE0_DIR}/src)
set(MINHOOK_DIR ${RE0_DIR}/third_party/minhook)
add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

set(SHARED_SOURCES
    ${SHARED_DIR}/net_client.cpp
    ${SHARED_DIR}/debug_stats.cpp
    ${SHARED_DIR}/log.cpp
    ${SHARED_DIR}/paths.cpp
    ${SHARED_DIR}/hooks.cpp)

add_library(sr2_adapter SHARED
    ${SHARED_SOURCES}
    src/proxy.cpp
    src/init.cpp
    src/config.cpp
    src/memory.cpp
    src/code_patch.cpp
    src/code_check.cpp
    src/limit_thunk.cpp
    src/coop_limits.cpp
    src/coop_pools.cpp
    src/replication_switch.cpp
    src/member_slots.cpp
    src/join_trace.cpp
    src/join_net_trace.cpp
    src/remote_players.cpp
    src/partner_scope.cpp
    src/guest_call.cpp
    src/partner_thunks_calls.cpp
    src/partner_thunks_session.cpp
    src/partner_thunks_gameplay.cpp
    src/partner_patches.cpp
    src/site_installer.cpp
    src/partner_identity.cpp
    src/activity_state.cpp
    src/activity_framework.cpp
    src/activity_credit.cpp
    src/activity_games.cpp
    src/activity_patches.cpp
    src/script_override.cpp
    src/content_hash.cpp
    src/browser_tag.cpp
    src/player_cap.cpp
    src/home_network.cpp
    src/game_port.cpp
    src/local_port_relay.cpp
    src/lan_address.cpp
    src/guest_welcome.cpp
    src/version_gate.cpp
    src/look_reset.cpp
    src/spawn_budget.cpp
    src/creation_queue.cpp
    src/leave_path.cpp
    src/multiplayer_patches.cpp
    src/multiplayer_thunks.cpp
    src/sender_scope.cpp
    src/join_admission.cpp
    src/pause_mask.cpp
    src/member_left.cpp
    src/cutscene_sync.cpp
    src/net_clock.cpp
    src/player_trace.cpp
    src/remote_frustum.cpp
    src/mission_players.cpp
    src/population.cpp
    src/game_socket.cpp
    src/tunnel.cpp
    src/join_file.cpp
    src/player_name.cpp
    src/join_status.cpp
    src/force_join.cpp
    src/force_join_guest.cpp
    src/force_join_lan.cpp
    ${RE0_DIR}/dinput8.def)
target_include_directories(sr2_adapter PRIVATE src ${SHARED_DIR})
# The shared logger writes <game>\coop\<this name>.
target_compile_definitions(sr2_adapter PRIVATE COOP_LOG_FILE=L"sr2_adapter.log")
set_target_properties(sr2_adapter PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_link_libraries(sr2_adapter PRIVATE minhook ws2_32 iphlpapi)
target_compile_options(sr2_adapter PRIVATE /W4 /WX /utf-8)
# The launcher and the release package take the DLL from here (git-ignored).
add_custom_command(TARGET sr2_adapter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sr2_adapter> ${CMAKE_CURRENT_SOURCE_DIR}/dinput8.dll)

# The partner thunks run inside game code that keeps floats on the x87 stack and may keep values in xmm registers across
# the calls they replace. They are built without SSE so that they never touch xmm; the few that do float math save the
# whole FPU state (fpu_guard.h).
set_source_files_properties(
    src/remote_players.cpp src/partner_scope.cpp src/guest_call.cpp
    src/partner_thunks_calls.cpp src/partner_thunks_session.cpp src/partner_thunks_gameplay.cpp src/player_cap.cpp
    src/join_net_trace.cpp src/player_name.cpp src/force_join.cpp src/force_join_guest.cpp src/force_join_lan.cpp src/tunnel.cpp src/home_network.cpp src/game_port.cpp src/local_port_relay.cpp src/lan_address.cpp src/guest_welcome.cpp src/version_gate.cpp src/look_reset.cpp src/spawn_budget.cpp src/leave_path.cpp
    src/sender_scope.cpp src/population.cpp src/creation_queue.cpp src/join_admission.cpp src/pause_mask.cpp src/member_left.cpp src/cutscene_sync.cpp src/net_clock.cpp src/player_trace.cpp src/remote_frustum.cpp src/mission_players.cpp
    src/partner_identity.cpp src/activity_state.cpp src/activity_framework.cpp src/activity_credit.cpp src/activity_games.cpp
    PROPERTIES COMPILE_OPTIONS /arch:IA32)

# Unit tests that run without the game, run by ctest: tests/<name>.cpp plus the listed sources.
enable_testing()
# Stand-in for the game in the one-PC tests of the Steam tunnel (tools/sr2/tunnel_adapter_test.py); not run by ctest.
add_executable(tunnel_standin tests/tunnel_standin.cpp src/tunnel.cpp src/lan_address.cpp src/code_patch.cpp src/memory.cpp
    ${SHARED_DIR}/net_client.cpp ${SHARED_DIR}/debug_stats.cpp ${SHARED_DIR}/log.cpp ${SHARED_DIR}/paths.cpp)
target_include_directories(tunnel_standin PRIVATE src ${SHARED_DIR})
target_compile_definitions(tunnel_standin PRIVATE COOP_LOG_FILE=L"tunnel_standin.log")
target_link_libraries(tunnel_standin PRIVATE ws2_32 iphlpapi)
target_compile_options(tunnel_standin PRIVATE /W4 /WX /utf-8)

function(add_unit_test name)
    add_executable(${name} tests/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE src ${SHARED_DIR})
    target_compile_options(${name} PRIVATE /W4 /WX /utf-8)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(config_test src/config.cpp ${SHARED_DIR}/paths.cpp)
add_unit_test(byte_pattern_test)
add_unit_test(pool_size_test)
add_unit_test(replication_sites_test)
add_unit_test(partner_rules_test)
add_unit_test(partner_sites_test)
add_unit_test(player_cap_test)
add_unit_test(join_net_trace_sites_test)
add_unit_test(trace_stub_test)
add_unit_test(home_network_rules_test)
add_unit_test(game_port_test)
add_unit_test(local_port_rules_test)
add_unit_test(lan_address_test)
add_unit_test(guest_welcome_test)
add_unit_test(look_reset_test)
add_unit_test(spawn_budget_test)
add_unit_test(leave_rules_test)
add_unit_test(trailer_rules_test)
add_unit_test(content_hash_rules_test)
add_unit_test(version_gate_sites_test)
add_unit_test(activity_rules_test)
add_unit_test(activity_sites_test)
add_unit_test(script_override_rules_test)
add_unit_test(join_file_test src/join_file.cpp)
add_unit_test(player_name_rules_test)
add_unit_test(force_join_names_test)
add_unit_test(join_deny_rules_test)
add_unit_test(tunnel_rules_test)
add_unit_test(tunnel_sites_test)
add_unit_test(browser_match_test)
add_unit_test(guest_join_rules_test)
add_unit_test(force_join_sites_test)
add_unit_test(guest_call_test src/guest_call.cpp)
add_unit_test(remote_players_test src/remote_players.cpp src/partner_scope.cpp src/member_slots.cpp src/replication_switch.cpp
    src/code_patch.cpp src/memory.cpp src/activity_state.cpp src/guest_call.cpp)
add_unit_test(activity_bindings_test src/activity_framework.cpp src/activity_credit.cpp src/activity_games.cpp src/activity_state.cpp
    src/partner_identity.cpp src/partner_thunks_calls.cpp src/partner_thunks_session.cpp src/partner_thunks_gameplay.cpp
    src/partner_scope.cpp src/remote_players.cpp src/member_slots.cpp src/replication_switch.cpp src/code_patch.cpp
    src/memory.cpp src/guest_call.cpp src/script_override.cpp ${SHARED_DIR}/paths.cpp)
add_unit_test(sender_scope_rules_test)
add_unit_test(population_rules_test)
add_unit_test(spread_rules_test)
add_unit_test(frustum_rules_test)
add_unit_test(handoff_rules_test)
add_unit_test(cutscene_sync_rules_test)
add_unit_test(pause_mask_rules_test)
add_unit_test(net_clock_rules_test)
add_unit_test(join_admission_rules_test)
add_unit_test(creation_queue_rules_test)
add_unit_test(multiplayer_sites_test)
add_unit_test(multiplayer_bindings_test src/multiplayer_thunks.cpp src/mission_players.cpp src/remote_frustum.cpp src/partner_thunks_calls.cpp src/partner_identity.cpp src/sender_scope.cpp src/population.cpp src/creation_queue.cpp src/join_admission.cpp src/pause_mask.cpp src/member_left.cpp src/cutscene_sync.cpp src/net_clock.cpp src/player_trace.cpp src/remote_frustum.cpp src/mission_players.cpp src/activity_state.cpp src/leave_path.cpp ${SHARED_DIR}/hooks.cpp src/partner_scope.cpp src/remote_players.cpp
    src/member_slots.cpp src/replication_switch.cpp src/code_patch.cpp src/memory.cpp src/guest_call.cpp src/spawn_budget.cpp)
target_link_libraries(multiplayer_bindings_test PRIVATE minhook)

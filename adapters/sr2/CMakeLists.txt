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
    src/player_cap.cpp
    src/home_network.cpp
    src/game_port.cpp
    ${RE0_DIR}/dinput8.def)
target_include_directories(sr2_adapter PRIVATE src ${SHARED_DIR})
# The shared logger writes <game>\coop\<this name>.
target_compile_definitions(sr2_adapter PRIVATE COOP_LOG_FILE=L"sr2_adapter.log")
set_target_properties(sr2_adapter PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_link_libraries(sr2_adapter PRIVATE minhook ws2_32)
target_compile_options(sr2_adapter PRIVATE /W4 /WX /utf-8)

# The partner thunks run inside game code that keeps floats on the x87 stack and may keep values in xmm registers across
# the calls they replace. They are built without SSE so that they never touch xmm; the few that do float math save the
# whole FPU state (fpu_guard.h).
set_source_files_properties(
    src/remote_players.cpp src/partner_scope.cpp src/guest_call.cpp
    src/partner_thunks_calls.cpp src/partner_thunks_session.cpp src/partner_thunks_gameplay.cpp src/player_cap.cpp
    src/join_net_trace.cpp src/home_network.cpp src/game_port.cpp
    src/partner_identity.cpp src/activity_state.cpp src/activity_framework.cpp src/activity_credit.cpp src/activity_games.cpp
    PROPERTIES COMPILE_OPTIONS /arch:IA32)

# Unit tests that run without the game, run by ctest: tests/<name>.cpp plus the listed sources.
enable_testing()
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
add_unit_test(activity_rules_test)
add_unit_test(activity_sites_test)
add_unit_test(script_override_rules_test)
add_unit_test(guest_call_test src/guest_call.cpp)
add_unit_test(remote_players_test src/remote_players.cpp src/partner_scope.cpp src/member_slots.cpp src/replication_switch.cpp
    src/code_patch.cpp src/memory.cpp src/activity_state.cpp src/guest_call.cpp)
add_unit_test(activity_bindings_test src/activity_framework.cpp src/activity_credit.cpp src/activity_games.cpp src/activity_state.cpp
    src/partner_identity.cpp src/partner_thunks_calls.cpp src/partner_thunks_session.cpp src/partner_thunks_gameplay.cpp
    src/partner_scope.cpp src/remote_players.cpp src/member_slots.cpp src/replication_switch.cpp src/code_patch.cpp
    src/memory.cpp src/guest_call.cpp src/script_override.cpp ${SHARED_DIR}/paths.cpp)

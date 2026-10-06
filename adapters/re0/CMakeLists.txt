cmake_minimum_required(VERSION 3.20)
project(coop_re0_adapter C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook)
add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx9.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

set(NET_SOURCES src/net_client.cpp src/debug_stats.cpp src/log.cpp src/paths.cpp)

add_library(re0_adapter SHARED
    ${NET_SOURCES}
    src/proxy.cpp
    src/init.cpp
    src/config.cpp
    src/state_sync.cpp
    src/vtable_tracer.cpp
    src/game_tick.cpp
    src/hooks.cpp
    src/character_owner.cpp
    src/inventory_sync.cpp
    src/equip_refresh.cpp
    src/enemy_action.cpp
    src/floor_items_sync.cpp
    src/floor_pending.cpp
    src/pickup_guard.cpp
    src/game_state.cpp
    src/door_travel.cpp
    src/door_sync.cpp
    src/event_place.cpp
    src/pad_commands.cpp
    src/scene.cpp
    src/split_rooms.cpp
    src/crash_dump.cpp
    src/net_trace.cpp
    src/flag_sync.cpp
    src/join_sync.cpp
    src/session_slot.cpp
    src/auto_join.cpp
    src/virtual_keys.cpp
    src/phase_watch.cpp
    src/menu_mirror.cpp
    src/camera_parity.cpp
    src/command_input.cpp
    src/command_log.cpp
    src/party_mode.cpp
    src/key_config.cpp
    src/window_focus.cpp
    src/partner_think.cpp
    src/net_pad.cpp
    src/input_record.cpp
    src/input_redirect.cpp
    src/state_correction.cpp
    src/save_redirect.cpp
    src/remote_storage_proxy.cpp
    src/damage_thunk.cpp
    src/enemy_registry.cpp
    src/enemy_damage_hook.cpp
    src/enemy_net.cpp
    src/enemy_state.cpp
    src/player_damage.cpp
    src/d3d9_hook.cpp
    src/debug_lines.cpp
    src/debug_overlay.cpp
    src/toast_queue.cpp
    dinput8.def)
set_target_properties(re0_adapter PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_link_libraries(re0_adapter PRIVATE minhook imgui ws2_32 dbghelp)
add_custom_command(TARGET re0_adapter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:re0_adapter> ${CMAKE_CURRENT_SOURCE_DIR}/dinput8.dll)

add_executable(net_test tests/net_test.cpp ${NET_SOURCES})
target_link_libraries(net_test PRIVATE ws2_32)

add_executable(proxy_test tests/proxy_test.cpp src/remote_storage_proxy.cpp)

add_executable(damage_thunk_test tests/damage_thunk_test.cpp src/damage_thunk.cpp)

add_executable(settled_copy_test tests/settled_copy_test.cpp)

add_executable(floor_pending_test tests/floor_pending_test.cpp src/floor_pending.cpp)

add_executable(overlay_test tests/overlay_test.cpp src/d3d9_hook.cpp
    src/debug_lines.cpp src/debug_overlay.cpp src/toast_queue.cpp src/hooks.cpp src/window_focus.cpp ${NET_SOURCES})
target_link_libraries(overlay_test PRIVATE minhook imgui d3d9 ws2_32)

add_executable(command_rules_test tests/command_rules_test.cpp src/key_config.cpp)

add_executable(door_phase_test tests/door_phase_test.cpp)

add_executable(position_blend_test tests/position_blend_test.cpp)

add_executable(equip_rule_test tests/equip_rule_test.cpp)

add_executable(enemy_action_rule_test tests/enemy_action_rule_test.cpp)

add_executable(enemy_puppet_rule_test tests/enemy_puppet_rule_test.cpp)


add_executable(flag_diff_test tests/flag_diff_test.cpp)

add_executable(jitter_target_test tests/jitter_target_test.cpp)

add_executable(pad_buffer_test tests/pad_buffer_test.cpp)

add_executable(trace_replay tools/trace_replay.cpp)

foreach(target re0_adapter net_test proxy_test damage_thunk_test settled_copy_test floor_pending_test overlay_test command_rules_test position_blend_test enemy_puppet_rule_test equip_rule_test enemy_action_rule_test door_phase_test flag_diff_test jitter_target_test pad_buffer_test trace_replay)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()

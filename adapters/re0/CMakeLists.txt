cmake_minimum_required(VERSION 3.20)
project(coop_re0_adapter C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
# Reproducible output (no timestamp or path in the objects and the DLL): tools/release_manifest.py reuses a component whose built files are byte for byte the previous release's.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Brepro>)
    add_link_options(/Brepro)
endif()

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

# DS2's pure authority table, compiled as is; its headers need DS2's src beside this adapter's own.
set(DS2_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ds2/src)
set(DS2_AUTHORITY_SOURCES ${DS2_SRC_DIR}/authority.cpp ${DS2_SRC_DIR}/reject_counters.cpp)

add_library(re0_adapter SHARED
    ${NET_SOURCES}
    src/proxy.cpp
    src/init.cpp
    src/config.cpp
    src/state_sync.cpp
    src/vtable_tracer.cpp
    src/game_tick.cpp
    src/slow_frame.cpp
    src/hooks.cpp
    src/character_owner.cpp
    src/inventory_sync.cpp
    src/equip_refresh.cpp
    src/enemy_decision.cpp
    src/enemy_spawn.cpp
    src/floor_items_sync.cpp
    src/floor_pending.cpp
    src/pickup_guard.cpp
    src/game_state.cpp
    src/door_travel.cpp
    src/door_sync.cpp
    src/room_gate.cpp
    src/event_place.cpp
    src/event_sync.cpp
    src/pad_commands.cpp
    src/scene.cpp
    src/split_rooms.cpp
    src/crash_dump.cpp
    src/net_trace.cpp
    src/flag_sync.cpp
    src/flag_sequencer.cpp
    src/world_epoch.cpp
    src/item_ledger.cpp
    src/prop_game.cpp
    src/prop_sync.cpp
    src/join_sync.cpp
    src/join_extras.cpp
    src/session_ready.cpp
    src/save_gate.cpp
    src/resync.cpp
    src/session_slot.cpp
    src/auto_join.cpp
    src/virtual_keys.cpp
    src/phase_watch.cpp
    src/menu_mirror.cpp
    src/live_menus.cpp
    src/appearance.cpp
    src/equip_steps.cpp
    src/partner_hud.cpp
    src/gui_text.cpp
    src/title_coop.cpp
    src/title_label.cpp
    src/coop_page.cpp
    src/coop_page_view.cpp
    src/partner_status.cpp
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
    src/pose_game.cpp
    src/pose_capture.cpp
    src/puppet.cpp
    src/puppet_hooks.cpp
    src/player_contact.cpp
    src/contact_game.cpp
    src/contact_net.cpp
    src/enemy_lease.cpp
    src/boss_handover.cpp
    src/state_mode.cpp
    src/state_net.cpp
    src/frame_router.cpp
    src/room_authority.cpp
    src/room_move.cpp
    src/door_follow.cpp
    src/event_body.cpp
    src/demo_skip.cpp
    src/world_digest.cpp
    src/dev_command.cpp
    src/rescue.cpp
    ${DS2_AUTHORITY_SOURCES}
    src/save_redirect.cpp
    src/remote_storage_proxy.cpp
    src/damage_thunk.cpp
    src/enemy_registry.cpp
    src/enemy_damage_hook.cpp
    src/enemy_net.cpp
    src/enemy_state.cpp
    src/enemy_target.cpp
    src/weapon_fx_game.cpp
    src/weapon_fx_capture.cpp
    src/weapon_fx_replay.cpp
    src/enemy_observer.cpp
    src/enemy_observer_capture.cpp
    src/enemy_observer_game.cpp
    src/player_damage.cpp
    src/d3d9_hook.cpp
    src/debug_lines.cpp
    src/debug_overlay.cpp
    src/toast_queue.cpp
    dinput8.def)
set_target_properties(re0_adapter PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
target_include_directories(re0_adapter PRIVATE src ${DS2_SRC_DIR})
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
    src/debug_lines.cpp src/debug_overlay.cpp src/toast_queue.cpp src/coop_page_view.cpp src/title_label.cpp src/hooks.cpp src/window_focus.cpp
    src/crash_dump.cpp src/game_tick.cpp src/slow_frame.cpp ${NET_SOURCES})
target_link_libraries(overlay_test PRIVATE minhook imgui d3d9 ws2_32 dbghelp)

add_executable(command_rules_test tests/command_rules_test.cpp src/key_config.cpp)

add_executable(door_phase_test tests/door_phase_test.cpp)

add_executable(position_blend_test tests/position_blend_test.cpp)

add_executable(equip_rule_test tests/equip_rule_test.cpp)

add_executable(appearance_rule_test tests/appearance_rule_test.cpp)

add_executable(spot_rule_test tests/spot_rule_test.cpp)

add_executable(enemy_follow_rule_test tests/enemy_follow_rule_test.cpp)

add_executable(room_gate_rule_test tests/room_gate_rule_test.cpp)

add_executable(spawn_seed_test tests/spawn_seed_test.cpp)

add_executable(event_rule_test tests/event_rule_test.cpp)


add_executable(session_rule_test tests/session_rule_test.cpp)

add_executable(menu_hold_rule_test tests/menu_hold_rule_test.cpp)

add_executable(live_menu_rule_test tests/live_menu_rule_test.cpp)

add_executable(partner_status_test tests/partner_status_test.cpp src/partner_status.cpp)

add_executable(gui_text_rule_test tests/gui_text_rule_test.cpp)

add_executable(title_coop_rule_test tests/title_coop_rule_test.cpp)

add_executable(title_label_rule_test tests/title_label_rule_test.cpp)

add_executable(coop_page_rule_test tests/coop_page_rule_test.cpp)

add_executable(flag_diff_test tests/flag_diff_test.cpp)

add_executable(flag_family_test tests/flag_family_test.cpp)

add_executable(jitter_target_test tests/jitter_target_test.cpp)

add_executable(pad_buffer_test tests/pad_buffer_test.cpp)

add_executable(enemy_hit_wire_test tests/enemy_hit_wire_test.cpp)

add_executable(target_hysteresis_rule_test tests/target_hysteresis_rule_test.cpp)

add_executable(pose_wire_test tests/pose_wire_test.cpp)

add_executable(pose_timeline_test tests/pose_timeline_test.cpp)

add_executable(puppet_rule_test tests/puppet_rule_test.cpp)

add_executable(state_net_rule_test tests/state_net_rule_test.cpp)

add_executable(frame_router_rule_test tests/frame_router_rule_test.cpp)

add_executable(load_serial_test tests/load_serial_test.cpp)

add_executable(stream_clock_test tests/stream_clock_test.cpp)

add_executable(timed_queue_test tests/timed_queue_test.cpp)

add_executable(pose_capture_rule_test tests/pose_capture_rule_test.cpp)

add_executable(flag_sequencer_test tests/flag_sequencer_test.cpp)

add_executable(world_epoch_rule_test tests/world_epoch_rule_test.cpp)

add_executable(flag_load_hold_rule_test tests/flag_load_hold_rule_test.cpp)

add_executable(item_ledger_test tests/item_ledger_test.cpp)

add_executable(item_wire_test tests/item_wire_test.cpp)

add_executable(prop_rule_test tests/prop_rule_test.cpp)

add_executable(prop_wire_test tests/prop_wire_test.cpp)

add_executable(room_authority_test tests/room_authority_test.cpp ${DS2_AUTHORITY_SOURCES})
target_include_directories(room_authority_test PRIVATE src ${DS2_SRC_DIR})

add_executable(weapon_fx_wire_test tests/weapon_fx_wire_test.cpp)

add_executable(weapon_fx_sites_test tests/weapon_fx_sites_test.cpp)

add_executable(weapon_fx_matrix_test tests/weapon_fx_matrix_test.cpp)

add_executable(enemy_policy_test tests/enemy_policy_test.cpp)

add_executable(enemy_observer_wire_test tests/enemy_observer_wire_test.cpp)
target_include_directories(enemy_observer_wire_test PRIVATE src ${DS2_SRC_DIR})

add_executable(enemy_observer_rule_test tests/enemy_observer_rule_test.cpp)
target_include_directories(enemy_observer_rule_test PRIVATE src ${DS2_SRC_DIR})

add_executable(enemy_observer_send_rule_test tests/enemy_observer_send_rule_test.cpp)

add_executable(room_move_wire_test tests/room_move_wire_test.cpp)

add_executable(room_move_rule_test tests/room_move_rule_test.cpp)

add_executable(slow_frame_test tests/slow_frame_test.cpp)

add_executable(contact_rule_test tests/contact_rule_test.cpp)

add_executable(enemy_lease_rule_test tests/enemy_lease_rule_test.cpp)

add_executable(enemy_lease_wire_test tests/enemy_lease_wire_test.cpp)
target_include_directories(enemy_lease_wire_test PRIVATE src ${DS2_SRC_DIR})

add_executable(demo_skip_test tests/demo_skip_test.cpp)

add_executable(boss_wire_test tests/boss_wire_test.cpp)
target_include_directories(boss_wire_test PRIVATE src ${DS2_SRC_DIR})

add_executable(boss_handover_test tests/boss_handover_test.cpp)
target_include_directories(boss_handover_test PRIVATE src ${DS2_SRC_DIR})

add_executable(session_ready_test tests/session_ready_test.cpp)
target_include_directories(session_ready_test PRIVATE src ${DS2_SRC_DIR})

add_executable(join_extras_test tests/join_extras_test.cpp)
target_include_directories(join_extras_test PRIVATE src ${DS2_SRC_DIR})

add_executable(save_gate_rule_test tests/save_gate_rule_test.cpp)

add_executable(hang_rule_test tests/hang_rule_test.cpp)

add_executable(dev_command_rule_test tests/dev_command_rule_test.cpp)

add_executable(world_digest_test tests/world_digest_test.cpp)

add_executable(rescue_rule_test tests/rescue_rule_test.cpp)

add_executable(fake_peer_wire_test tests/fake_peer_wire_test.cpp)
target_include_directories(fake_peer_wire_test PRIVATE src ${DS2_SRC_DIR})
target_compile_definitions(fake_peer_wire_test PRIVATE FAKE_PEER_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/../../tools/re0/fake_peer/tests/golden_frames.bin")

add_executable(trace_replay tools/trace_replay.cpp)

foreach(target re0_adapter net_test proxy_test damage_thunk_test settled_copy_test floor_pending_test overlay_test command_rules_test position_blend_test enemy_follow_rule_test room_gate_rule_test spawn_seed_test equip_rule_test appearance_rule_test spot_rule_test door_phase_test session_rule_test menu_hold_rule_test live_menu_rule_test partner_status_test gui_text_rule_test title_coop_rule_test title_label_rule_test coop_page_rule_test flag_diff_test flag_family_test jitter_target_test pad_buffer_test enemy_hit_wire_test target_hysteresis_rule_test event_rule_test pose_wire_test pose_timeline_test puppet_rule_test state_net_rule_test frame_router_rule_test load_serial_test stream_clock_test timed_queue_test pose_capture_rule_test flag_sequencer_test world_epoch_rule_test flag_load_hold_rule_test room_authority_test weapon_fx_wire_test weapon_fx_sites_test weapon_fx_matrix_test enemy_policy_test enemy_observer_wire_test enemy_observer_rule_test enemy_observer_send_rule_test item_ledger_test item_wire_test prop_rule_test prop_wire_test room_move_wire_test room_move_rule_test slow_frame_test contact_rule_test enemy_lease_rule_test enemy_lease_wire_test demo_skip_test boss_wire_test boss_handover_test session_ready_test join_extras_test save_gate_rule_test hang_rule_test dev_command_rule_test world_digest_test rescue_rule_test fake_peer_wire_test trace_replay)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()

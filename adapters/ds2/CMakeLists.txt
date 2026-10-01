cmake_minimum_required(VERSION 3.20)
project(coop_ds2_adapter C CXX)

# x64 only (DEATH STRANDING 2 is a 64-bit DX12 game). Build from a VsDevCmd -arch=amd64 shell.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# Shared with the RE0 adapter: launcher link, logging, crash dumps, MinHook wrapper, vendored MinHook and ImGui.
set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../re0/src)
set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../re0/third_party)

set(MINHOOK_DIR ${THIRD_PARTY}/minhook)
add_library(minhook64 STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook64 PUBLIC ${MINHOOK_DIR}/include)

set(IMGUI_DIR ${THIRD_PARTY}/imgui)
add_library(imgui_dx12 STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(imgui_dx12 PUBLIC ${IMGUI_DIR})

set(SHARED_SOURCES
    ${SHARED_DIR}/net_client.cpp
    ${SHARED_DIR}/debug_stats.cpp
    ${SHARED_DIR}/log.cpp
    ${SHARED_DIR}/paths.cpp
    ${SHARED_DIR}/hooks.cpp
    ${SHARED_DIR}/crash_dump.cpp)

add_library(ds2_adapter SHARED
    ${SHARED_SOURCES}
    src/proxy.cpp
    src/init.cpp
    src/config.cpp
    src/documents_redirect.cpp
    src/import_patch.cpp
    src/input_filter.cpp
    src/player_sync.cpp
    src/dx12_hook.cpp
    src/marker_overlay.cpp
    src/cargo_transfer.cpp
    src/cargo_ground.cpp
    src/cargo_menu.cpp
    src/remote_body.cpp
    src/main_thread.cpp
    ${SHARED_DIR}/toast_queue.cpp
    src/pattern_scan.cpp
    src/msvc_rtti.cpp
    src/decima/safe_read.cpp
    src/decima/entity.cpp
    src/decima/localized_text.cpp
    src/ds2/game.cpp
    src/ds2/body.cpp
    src/ds2/use_gate.cpp
    src/ds2/cargo.cpp)
target_include_directories(ds2_adapter PRIVATE src ${SHARED_DIR})
set_target_properties(ds2_adapter PROPERTIES OUTPUT_NAME version PREFIX "")
target_link_libraries(ds2_adapter PRIVATE minhook64 imgui_dx12 ws2_32 dbghelp d3d12 dxgi ole32)
add_custom_command(TARGET ds2_adapter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ds2_adapter> ${CMAKE_CURRENT_SOURCE_DIR}/version.dll)

add_executable(world_to_screen_test tests/world_to_screen_test.cpp)

add_executable(overlay_test tests/overlay_test.cpp src/dx12_hook.cpp ${SHARED_SOURCES})
target_include_directories(overlay_test PRIVATE src ${SHARED_DIR})
target_link_libraries(overlay_test PRIVATE minhook64 imgui_dx12 ws2_32 dbghelp d3d12 dxgi)

add_executable(proxy_load_test tests/proxy_load_test.cpp)
add_dependencies(proxy_load_test ds2_adapter)

foreach(target ds2_adapter world_to_screen_test proxy_load_test overlay_test)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()

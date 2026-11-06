cmake_minimum_required(VERSION 3.20)
project(coop_cd_adapter C CXX)

# x64 only (Crimson Desert is a 64-bit DX12 game). Build from a VsDevCmd -arch=amd64 shell.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
# /EHa: a hardware fault caught by an __except in a hook still unwinds C++ locals, so no lock stays held.
string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# Reproducible output: tools/release_manifest.py reuses a component whose built files match the previous release's.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Brepro>)
    add_link_options(/Brepro)
endif()

# Shared with the RE0 and DS2 adapters: launcher link, logging, crash dumps, MinHook wrapper, vendored MinHook.
set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../re0/src)
set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../re0/third_party/minhook)
add_library(minhook64 STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook64 PUBLIC ${MINHOOK_DIR}/include)

add_library(cd_adapter SHARED
    ${SHARED_DIR}/net_client.cpp
    ${SHARED_DIR}/debug_stats.cpp
    ${SHARED_DIR}/log.cpp
    ${SHARED_DIR}/paths.cpp
    ${SHARED_DIR}/hooks.cpp
    ${SHARED_DIR}/crash_dump.cpp
    src/main.cpp
    src/init.cpp
    src/config.cpp
    src/build_gate.cpp
    src/safe_read.cpp
    src/troc_catalogue.cpp
    src/funnels.cpp
    src/capture.cpp
    src/main_tick.cpp
    src/world_gate.cpp
    src/game_state.cpp
    src/link.cpp
    src/self_hash.cpp
    src/census.cpp
    src/dev_cmd.cpp
    src/presence_send.cpp
    src/presence_recv.cpp
    src/equipment.cpp
    src/minimap.cpp
    src/containment.cpp
    src/world_time.cpp
    src/story.cpp
    src/stage_guard.cpp
    src/save_guard.cpp)
target_include_directories(cd_adapter PRIVATE src ${SHARED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set_target_properties(cd_adapter PROPERTIES OUTPUT_NAME winmm PREFIX "")
target_link_libraries(cd_adapter PRIVATE minhook64 ws2_32 dbghelp bcrypt)
add_custom_command(TARGET cd_adapter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:cd_adapter> ${CMAKE_CURRENT_SOURCE_DIR}/winmm.dll)

add_executable(rules_test tests/rules_test.cpp)
target_include_directories(rules_test PRIVATE ${SHARED_DIR})

foreach(target cd_adapter rules_test)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()

enable_testing()
add_test(NAME rules_test COMMAND rules_test)

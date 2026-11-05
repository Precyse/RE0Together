cmake_minimum_required(VERSION 3.20)
project(coop_dd2_plugin C CXX)

# x64 only: a REFramework C++ plugin (reframework\plugins\dd2coop.dll) for Dragon's Dogma 2. Build from a
# VsDevCmd -arch=amd64 shell.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
# /EHa: a hardware fault caught by an __except in a callback still unwinds C++ locals, so no lock stays held.
string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# Reproducible output: tools/release_manifest.py reuses a component whose built files match the previous release's.
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/Brepro>)
    add_link_options(/Brepro)
endif()

# Shared with the other adapters: launcher link, logging, crash dumps (adapters/re0/src) and adapters/common.
set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../re0/src)
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)

add_library(dd2coop SHARED
    ${SHARED_DIR}/net_client.cpp
    ${SHARED_DIR}/debug_stats.cpp
    ${SHARED_DIR}/log.cpp
    ${SHARED_DIR}/paths.cpp
    ${SHARED_DIR}/crash_dump.cpp
    src/plugin_entry.cpp
    src/refw.cpp
    src/hook_registry.cpp
    src/build_gate.cpp
    src/config.cpp
    src/world_gate.cpp
    src/session.cpp
    src/tick.cpp
    src/calls.cpp
    src/local_body.cpp
    src/owner_stream.cpp
    src/peer_bodies.cpp
    src/world_clock.cpp
    src/offline.cpp
    src/echo_puppet.cpp
    src/look.cpp
    src/puppet_builder.cpp
    src/body_card.cpp
    src/peer_puppet.cpp
    src/puppet_registry.cpp
    src/puppet_brain.cpp
    src/puppet_driver.cpp
    src/serializer.cpp
    src/save_sections.cpp
    src/profile.cpp
    src/save_redirect.cpp
    src/plague_off.cpp
    src/roster.cpp
    src/enemy_spawn.cpp
    src/enemy_stream.cpp
    src/enemy_mirror.cpp)
target_include_directories(dd2coop PRIVATE src ${SHARED_DIR} ${COMMON_DIR})
set_target_properties(dd2coop PROPERTIES PREFIX "")
target_link_libraries(dd2coop PRIVATE ws2_32 dbghelp)
add_custom_command(TARGET dd2coop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dd2coop> ${CMAKE_CURRENT_SOURCE_DIR}/dd2coop.dll)

add_executable(rules_test tests/rules_test.cpp)
target_include_directories(rules_test PRIVATE ${SHARED_DIR})

foreach(target dd2coop rules_test)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
endforeach()

enable_testing()
add_test(NAME rules_test COMMAND rules_test)

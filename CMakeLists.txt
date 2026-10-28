cmake_minimum_required(VERSION 3.16)
project(openams_ui C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Languages. -DMMU_LANGUAGES=<set> picks what this build carries: every string
# table and every CJK face of the set, and nothing else. Defaults to every
# language, which is what the simulator, the browser build and the tests have
# always built. The same module drives the ESP32 component and the wasm core.
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(languages)
# The UI's resolution (-DMMU_UI_RESOLUTION=480x320|800x480): which font set the
# UI builds with and how it scales its 480x320 grid. Same module as the ESP32.
include(resolution)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# LVGL (submodule in lib/lvgl, never modified)
# ---------------------------------------------------------------------------
# Use the one shared lv_conf.h at the repo root, and strip the parts of LVGL
# that are not wanted in the firmware image (demos, examples, ThorVG).
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/lvgl)

# ---------------------------------------------------------------------------
# SDL2 — simulator platform only.
# LV_USE_SDL is passed as a compile definition; lv_conf.h guards it with
# #ifndef so the same file stays valid on ESP32 (where SDL is absent).
#
# Native Linux build: SDL2 comes from pkg-config, as before. Cross-compiling
# for Windows (CMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake, WIN32 true)
# has no pkg-config sdl2.pc on the host, so it finds the SDL2 devel package's
# own CMake config instead (point SDL2_DIR, or CMAKE_PREFIX_PATH, at
# .../x86_64-w64-mingw32).
# Emscripten (CMAKE_TOOLCHAIN_FILE=$EMSDK/.../Emscripten.cmake, EMSCRIPTEN
# true) needs neither: -sUSE_SDL=2 makes emcc supply its own SDL2 port at
# compile+link time, so there is nothing to find_package/pkg_check_modules.
# ---------------------------------------------------------------------------
target_compile_definitions(lvgl PUBLIC LV_USE_SDL=1)
# lv_conf.h sizes LVGL's pool by resolution: LVGL and the app must agree.
target_compile_definitions(lvgl PUBLIC ${MMU_UI_DEFINES})

if(EMSCRIPTEN)
    target_compile_options(lvgl PUBLIC -sUSE_SDL=2)
elseif(WIN32)
    find_package(SDL2 REQUIRED CONFIG)
    target_link_libraries(lvgl PUBLIC SDL2::SDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)

    target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_directories(lvgl PUBLIC ${SDL2_LIBRARY_DIRS})
    target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    # Browser build: platform/web/main.c instead of platform/sim/main.c, plus
    # the one non-SDL sim file it still needs (display settings are a no-op
    # struct there too). No gallery/audit/BMP-encoder sources: those are
    # --smoke/--screenshots/--gallery tooling the page never calls.
    file(GLOB_RECURSE MMU_WEB_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/*.c"
    )
    list(APPEND MMU_WEB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/platform/sim/display_cfg.c")
    mmu_languages_filter_sources(MMU_WEB_SOURCES ${MMU_WEB_SOURCES})

    mmu_resolution_filter_sources(MMU_WEB_SOURCES ${MMU_WEB_SOURCES})

    add_executable(openams-ui-web ${MMU_WEB_SOURCES})

    target_include_directories(openams-ui-web PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    )

    target_compile_definitions(openams-ui-web PRIVATE ${MMU_LANGUAGE_DEFINES} ${MMU_UI_DEFINES})

    target_compile_options(openams-ui-web PRIVATE
        -Wall -Wextra
        -Wformat-truncation=1 -Werror=format-truncation
        -Werror=format-overflow
        -Os -sUSE_SDL=2)

    target_link_libraries(openams-ui-web PRIVATE lvgl::lvgl m)

    # site/openams-ui-web.js + site/openams-ui-web.wasm: no HTML shell (we
    # ship our own index.html, see platform/web/index.html), no filesystem
    # (nothing is loaded at runtime beyond the compiled-in fonts), grown heap
    # instead of a fixed ceiling since the browser — unlike the CYD — has
    # memory to spare.
    set_target_properties(openams-ui-web PROPERTIES SUFFIX ".js")
    target_link_options(openams-ui-web PRIVATE
        -Os -sUSE_SDL=2
        -sALLOW_MEMORY_GROWTH=1
        -sASSERTIONS=0
        "-sEXPORTED_FUNCTIONS=['_main','_web_set_scenario','_web_set_unit']"
        "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap']")
else()
    file(GLOB_RECURSE MMU_APP_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sim/*.c"
    )
    mmu_languages_filter_sources(MMU_APP_SOURCES ${MMU_APP_SOURCES})

    mmu_resolution_filter_sources(MMU_APP_SOURCES ${MMU_APP_SOURCES})

    add_executable(openams-ui-sim ${MMU_APP_SOURCES})

    target_include_directories(openams-ui-sim PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    )

    target_compile_definitions(openams-ui-sim PRIVATE ${MMU_LANGUAGE_DEFINES} ${MMU_UI_DEFINES})

    # Warnings are on for our code only; LVGL keeps its own (quieter) flags.
    # format-truncation/format-overflow are promoted to errors here so a
    # snprintf() buffer too small for its worst-case %d/%s fails THIS build,
    # rather than reaching ESP-IDF's own -Werror=format-truncation and failing
    # only the firmware.
    target_compile_options(openams-ui-sim PRIVATE
        -Wall -Wextra
        -Wformat-truncation=1 -Werror=format-truncation
        -Werror=format-overflow)

    target_link_libraries(openams-ui-sim PRIVATE lvgl::lvgl m)

    # Cross build for a tester with no MinGW runtime installed: static libgcc
    # (and, on the posix thread-model toolchain used here, static winpthread) so
    # SDL2.dll is the only DLL the exe needs beside the ones Windows ships.
    if(WIN32)
        target_link_options(openams-ui-sim PRIVATE -static-libgcc -static)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests. The data model is pure C with no LVGL or platform dependency, so a
# test links its .c directly instead of pulling in the simulator. Native only:
# the Emscripten toolchain has no way to run a CTest binary.
# ---------------------------------------------------------------------------
if(NOT EMSCRIPTEN)
    enable_testing()

    add_executable(test_model_live
        tests/test_model_live.c
        src/model/mmu_model.c
        src/backend/status_map.c
        src/view/strings.c    # the mapper's alerts are whole-sentence templates
        src/lib/cjson/cJSON.c)

    target_include_directories(test_model_live PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_options(test_model_live PRIVATE -Wall -Wextra)

    add_test(NAME model_live COMMAND test_model_live)

    # The status mapper is pure C too: cJSON plus the model it fills, no LVGL.
    # The fixture directory is a compile definition so the test does not
    # depend on the working directory CTest happens to run it from.
    add_executable(test_status_map
        tests/test_status_map.c
        src/backend/status_map.c
        src/lib/cjson/cJSON.c
        src/model/mmu_model.c
        src/view/strings.c)   # the mapper's reasons are view strings

    target_include_directories(test_status_map PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_definitions(test_status_map PRIVATE
        "STATUS_MAP_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")

    target_compile_options(test_status_map PRIVATE -Wall -Wextra)

    add_test(NAME status_map COMMAND test_status_map)

    # The core is pure C too: cJSON plus the status mapper and the model it
    # fills, no LVGL.
    add_executable(test_core
        tests/test_core.c
        src/core/core.c
        src/backend/status_map.c
        src/lib/cjson/cJSON.c
        src/model/mmu_model.c
        src/view/strings.c)   # core_set_language() forwards to the string tables

    target_include_directories(test_core PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_options(test_core PRIVATE -Wall -Wextra)

    add_test(NAME core COMMAND test_core)

    # The action mapper is pure C too: the model plus the status mapper that
    # builds it, no LVGL. It is fed a recorded fixture, whose slot ids, lane and
    # host ids are the recorded ones, so its fixture directory is a compile
    # definition for the same reason as above.
    add_executable(test_actions_map
        tests/test_actions_map.c
        src/backend/actions_map.c
        src/backend/status_map.c
        src/lib/cjson/cJSON.c
        src/model/mmu_model.c
        src/view/strings.c)   # the mapper's reasons are view strings

    target_include_directories(test_actions_map PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_definitions(test_actions_map PRIVATE
        "ACTIONS_MAP_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/recorded\"")

    target_compile_options(test_actions_map PRIVATE -Wall -Wextra)

    add_test(NAME actions_map COMMAND test_actions_map)

    # The view builders (src/view) are pure C too: cJSON plus the core and the
    # model/status mapper that build it, no LVGL. Struct sizes and no-alloc
    # checks need no fixture; the shortfall test builds its own tiny model.
    add_executable(test_view
        tests/test_view.c
        src/view/view.c
        src/view/strings.c
        src/view/view_json.c
        src/core/core.c
        src/backend/actions_map.c
        src/backend/status_map.c
        src/lib/cjson/cJSON.c
        src/model/mmu_model.c)

    target_include_directories(test_view PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    # The recorded fixtures, for the test that walks every one of them looking
    # for a cut string; a compile definition so it does not depend on the
    # working directory CTest happens to run it from.
    target_compile_definitions(test_view PRIVATE
        "VIEW_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/recorded\"")

    target_compile_options(test_view PRIVATE -Wall -Wextra)

    add_test(NAME view COMMAND test_view)

    # The Moonraker client is transport-independent pure C too: cJSON plus a
    # scripted WebSocket, no LVGL and no socket. It copies the bound unit's
    # name with the model's own character-safe copy, so the model is here too.
    # The fixture directory is a compile definition for the same reason as
    # above.
    add_executable(test_moonraker_client
        tests/test_moonraker_client.c
        src/backend/moonraker_client.c
        src/model/mmu_model.c
        src/view/strings.c    # the model's texts are string-table templates
        src/lib/cjson/cJSON.c)

    target_include_directories(test_moonraker_client PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_definitions(test_moonraker_client PRIVATE
        "MOONRAKER_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")

    target_compile_options(test_moonraker_client PRIVATE -Wall -Wextra)

    add_test(NAME moonraker_client COMMAND test_moonraker_client)

    # OTA policy is pure C too: no ESP-IDF headers, no I/O.
    add_executable(test_ota_policy
        tests/test_ota_policy.c
        src/app/ota_policy.c)

    target_include_directories(test_ota_policy PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    target_compile_options(test_ota_policy PRIVATE -Wall -Wextra)

    add_test(NAME ota_policy COMMAND test_ota_policy)

    # The language list itself: what -DMMU_LANGUAGES built into this binary.
    # It is compiled with the same definitions as the simulator, and the codes
    # the build asked for come in as a compile definition, so a restricted build
    # checks its own list rather than a fixed one.
    add_executable(test_languages
        tests/test_languages.c
        src/view/strings.c)

    target_include_directories(test_languages PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src")

    string(REPLACE ";" "," MMU_LANGUAGES_CSV "${MMU_LANGUAGE_CODES}")
    target_compile_definitions(test_languages PRIVATE
        ${MMU_LANGUAGE_DEFINES} ${MMU_UI_DEFINES}
        "MMU_TEST_LANGS=\"${MMU_LANGUAGES_CSV}\"")

    target_compile_options(test_languages PRIVATE -Wall -Wextra)

    add_test(NAME languages COMMAND test_languages)

    # The spool editor itself, not the view's form of it: LVGL widgets, real
    # events and a real Save, so the `spool edit` line the device sends is the
    # one under test. Everything but the simulator's main() - the screens, the
    # theme, the model, the mappers - plus the platform hook the line lands in.
    file(GLOB_RECURSE MMU_EDIT_SCREEN_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    )
    list(APPEND MMU_EDIT_SCREEN_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sim/display_cfg.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_edit.c")
    mmu_languages_filter_sources(MMU_EDIT_SCREEN_SOURCES ${MMU_EDIT_SCREEN_SOURCES})

    mmu_resolution_filter_sources(MMU_EDIT_SCREEN_SOURCES ${MMU_EDIT_SCREEN_SOURCES})

    add_executable(test_screen_edit ${MMU_EDIT_SCREEN_SOURCES})

    target_include_directories(test_screen_edit PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform")

    target_compile_definitions(test_screen_edit PRIVATE ${MMU_LANGUAGE_DEFINES} ${MMU_UI_DEFINES})

    target_compile_options(test_screen_edit PRIVATE -Wall -Wextra)

    target_link_libraries(test_screen_edit PRIVATE lvgl::lvgl m)

    add_test(NAME screen_edit COMMAND test_screen_edit)
    set_tests_properties(screen_edit PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    # The same screens again, for the one rule the in-place update has to keep:
    # after update() answers true, the screen is pixel-identical to a fresh
    # build of the same model. Real snapshots of real widgets, both built and
    # updated, compared byte for byte.
    file(GLOB_RECURSE MMU_SCREEN_UPDATE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    )
    list(APPEND MMU_SCREEN_UPDATE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/sim/display_cfg.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_screen_update.c")
    mmu_languages_filter_sources(MMU_SCREEN_UPDATE_SOURCES ${MMU_SCREEN_UPDATE_SOURCES})

    mmu_resolution_filter_sources(MMU_SCREEN_UPDATE_SOURCES ${MMU_SCREEN_UPDATE_SOURCES})

    add_executable(test_screen_update ${MMU_SCREEN_UPDATE_SOURCES})

    target_include_directories(test_screen_update PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform")

    target_compile_definitions(test_screen_update PRIVATE ${MMU_LANGUAGE_DEFINES} ${MMU_UI_DEFINES})

    target_compile_options(test_screen_update PRIVATE -Wall -Wextra)

    target_link_libraries(test_screen_update PRIVATE lvgl::lvgl m)

    add_test(NAME screen_update COMMAND test_screen_update)
    set_tests_properties(screen_update PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy")

    # Replay the recorded fixtures through the simulator and compare each
    # model dump against its golden (tests/fixtures/recorded/*.dump). The
    # simulator is a path argument because the script is shared with machines
    # that only build it here.
    add_test(NAME replay_vams_fixtures
        COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_vams_fixtures.sh"
                $<TARGET_FILE:openams-ui-sim>
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    # Same idea, for the view builders' JSON instead of the model dump
    # (tests/fixtures/recorded/*.view.json).
    add_test(NAME replay_view_goldens COMMAND bash
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/replay_view_goldens.sh"
        $<TARGET_FILE:openams-ui-sim>
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    # The four-unit live-host fixture walked through every screen and modal with
    # the layout audit, once per language this build carries: it is the widest
    # real model (six units after the walk's own scenarios), the one that ran
    # the LVGL pool out. A pool that fills up crashes inside LVGL, so this is
    # the guard on the peak, and the audit must find nothing. Labelled "slow".
    foreach(lang IN LISTS MMU_LANGUAGE_CODES)
        if(lang STREQUAL "qps")   # the pseudo-locale overflows by design: not audited
            continue()
        endif()
        add_test(NAME smoke_four_units_${lang}
            COMMAND openams-ui-sim --replay
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/recorded/four_units.json"
                    --smoke --audit --lang ${lang})
        set_tests_properties(smoke_four_units_${lang} PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            PASS_REGULAR_EXPRESSION "SMOKE OK.*AUDIT TOTAL issues=0"
            LABELS slow TIMEOUT 600)
    endforeach()

    # Every openams_ui payload of the fixtures against the host's own schema
    # (found through OPENAMS_ROOT or a sibling checkout; skipped without it).
    add_test(NAME vams_fixtures_schema COMMAND python3
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/validate_vams_fixtures.py")
    set_tests_properties(vams_fixtures_schema PROPERTIES SKIP_RETURN_CODE 77)

    # The goldens were recorded from a build carrying every language, and the
    # settings view lists the languages of the image, so a -DMMU_LANGUAGES build
    # compares without that one list. It is told which build it is looking at.
    string(REPLACE ";" "\\;" MMU_LANGUAGES_TEST "${MMU_LANGUAGES}")
    set_tests_properties(replay_view_goldens PROPERTIES
        ENVIRONMENT "MMU_LANGUAGES=${MMU_LANGUAGES_TEST}")
endif()

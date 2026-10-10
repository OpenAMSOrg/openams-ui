# openams-core: the UI core (model, merge-owning core, status/action mappers,
# view builders and their JSON) as one WebAssembly module for a web host
# (docs/design/UNIFIED_UI.md 8). No LVGL, no SDL, no Moonraker client, no UI.
#
# A standalone project, not a target of the top-level CMakeLists.txt: the
# display build configures LVGL and SDL on configure, and this one must not.
#
#   source ~/emsdk/emsdk_env.sh
#   bash platform/core-wasm/build.sh        # -> web/core/dist/openams-core.{js,wasm}
cmake_minimum_required(VERSION 3.16)
project(openams_core C)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "openams-core is an Emscripten target: configure it with emcmake "
                        "(see platform/core-wasm/build.sh)")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# The languages this module carries, default every one of them: the web host
# shows all of them, so the wasm core is the oracle for all of them. The same
# module and the same -DMMU_LANGUAGES as the simulator and the firmware, for a
# core built for one language.
include("${REPO_ROOT}/cmake/languages.cmake")

# The C stack, in bytes (the budget check: a smaller stack is tried with
# -DOPENAMS_CORE_STACK=<bytes>).
set(OPENAMS_CORE_STACK 65536 CACHE STRING "openams-core C stack size in bytes")

add_executable(openams-core
    core_wasm.c
    "${REPO_ROOT}/src/core/core.c"
    "${REPO_ROOT}/src/model/mmu_model.c"
    "${REPO_ROOT}/src/backend/status_map.c"
    "${REPO_ROOT}/src/backend/actions_map.c"
    "${REPO_ROOT}/src/view/view.c"
    "${REPO_ROOT}/src/view/view_json.c"
    "${REPO_ROOT}/src/view/strings.c"
    "${REPO_ROOT}/src/lib/cjson/cJSON.c")

target_include_directories(openams-core PRIVATE "${REPO_ROOT}" "${REPO_ROOT}/src")

target_compile_definitions(openams-core PRIVATE ${MMU_LANGUAGE_DEFINES})

# -Os for size. -fno-math-errno keeps the math wrappers out.
target_compile_options(openams-core PRIVATE
    -Wall -Wextra
    -Wformat-truncation -Werror=format-truncation
    -Werror=format-overflow
    -Os)

# A fixed 4 MB linear memory that never grows (the budget, and so a runaway
# input fails cleanly instead of eating the tab), of which the C stack takes
# 64 KB (STACK_SIZE): the JSON path's deepest frame is a view_actions_t
# (about 7 KB, view_json.c gives each one its own frame) under a cJSON
# print/parse recursion. web/core/test/stack.test.mjs renders the largest
# golden (two_lanes). No filesystem, no assertions, no glue for anything the
# shim does not use. _malloc/_free are exported so the JS side can copy a
# large JSON into the heap (ccall's 'string' arguments would land on the
# stack, which is far too small for a spool list). The six core_wasm_*
# functions are the shim (core_wasm.c).
set_target_properties(openams-core PROPERTIES SUFFIX ".js")
target_link_options(openams-core PRIVATE
    -Os
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createOpenamsCore
    -sENVIRONMENT=web,node
    -sINITIAL_MEMORY=4MB
    -sALLOW_MEMORY_GROWTH=0
    -sSTACK_SIZE=${OPENAMS_CORE_STACK}
    -sFILESYSTEM=0
    -sASSERTIONS=0
    -sNO_EXIT_RUNTIME=1
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_core_wasm_init','_core_wasm_reset','_core_wasm_apply','_core_wasm_view','_core_wasm_action','_core_wasm_set_language','_core_wasm_free']"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8']")

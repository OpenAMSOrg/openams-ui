cmake_minimum_required(VERSION 3.16)

# lib/ holds the LVGL component (a symlink to the shared, never-modified
# submodule checkout) plus lib/lv_conf.h, which includes the one lv_conf.h
# shared with the SDL simulator. lib/lvgl/env_support/cmake/esp.cmake
# registers the "lvgl" component with INCLUDE_DIRS that include its own
# parent directory (lib/), which is how lv_conf.h reaches it under
# LV_CONF_INCLUDE_SIMPLE without editing anything under lib/lvgl/.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../lib")

# The languages this image carries. The device default is `latin` (the eight
# Latin and Cyrillic languages), not every language: an image that carried them
# all left too little of the OTA slot free. -DMMU_LANGUAGES=ja / ko / zh-Hans /
# all / "en;ja" picks another set; cmake/languages.cmake is shared with the
# simulator and the wasm core.
if(NOT DEFINED MMU_LANGUAGES)
    set(MMU_LANGUAGES "latin" CACHE STRING
        "Languages this image carries: a preset (all, latin, ja, ko, zh-Hans) \
or a ';' list of codes.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/languages.cmake")

# The app version carries the language set, so a user who flashed -ja over
# -latin can tell the two images apart in the OTA console (main/ota.c prints
# desc.version) and so does esp_ota_get_partition_description(). Set the base
# with -DMMU_APP_VERSION=<version>; the tag follows it.
set(MMU_APP_VERSION "1.0.0" CACHE STRING
    "Base app version; the language tag (-latin, -ja, ...) is appended.")
set(PROJECT_VER "${MMU_APP_VERSION}-${MMU_LANGUAGE_TAG}")
string(LENGTH "${PROJECT_VER}" mmu_version_len)
if(mmu_version_len GREATER 31)   # esp_app_desc_t.version is 32 bytes
    message(FATAL_ERROR "PROJECT_VER \"${PROJECT_VER}\" is ${mmu_version_len} characters; "
                        "the app description holds 31. Shorten MMU_APP_VERSION.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openams_ui_esp32)

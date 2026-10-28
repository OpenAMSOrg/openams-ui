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

# The spool tag writer (tag_writer/, docs/guide/tag-writer.md): a menu entry
# that composes a filament spool and writes it to an RFID tag, reads any
# filament tag, and resets, copies and clones Bambu tags, with a PN5180 or an
# MFRC522 on the board's VSPI connector. On by default; -DMMU_SPOOL_TAG_WRITER=OFF
# leaves it out and the image is exactly the display alone.
option(MMU_SPOOL_TAG_WRITER "Build the spool tag writer into the image" ON)
set(MMU_TAG_READER "PN5180" CACHE STRING
    "The tag writer's reader: PN5180 (every tag type and format) or MFRC522 (Bambu MIFARE only)")
set_property(CACHE MMU_TAG_READER PROPERTY STRINGS PN5180 MFRC522)
if(NOT MMU_TAG_READER MATCHES "^(PN5180|MFRC522)$")
    message(FATAL_ERROR "MMU_TAG_READER is \"${MMU_TAG_READER}\"; it takes PN5180 or MFRC522")
endif()
# The display board (main/board.h): the default E32R40T (LCDWiki 4.0" ESP32,
# 480x320 SPI panel, resistive touch) or ES3C50P (LCDWiki / Hosyond 5.0"
# ESP32-S3 N16R8, 800x480 RGB panel, capacitive touch). The board picks the
# chip, so -DMMU_BOARD=es3c50p is all a build needs.
set(MMU_BOARD "e32r40t" CACHE STRING "The display board: e32r40t or es3c50p")
set_property(CACHE MMU_BOARD PROPERTY STRINGS e32r40t es3c50p)
if(NOT MMU_BOARD MATCHES "^(e32r40t|es3c50p)$")
    message(FATAL_ERROR "MMU_BOARD is \"${MMU_BOARD}\"; it takes e32r40t or es3c50p")
endif()

if(MMU_SPOOL_TAG_WRITER)
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/tag_writer")
endif()

# The UI's resolution follows the panel (cmake/resolution.cmake).
if(MMU_BOARD STREQUAL "es3c50p")
    set(MMU_UI_RESOLUTION "800x480" CACHE STRING "The UI's resolution: 480x320 or 800x480" FORCE)
else()
    set(MMU_UI_RESOLUTION "480x320" CACHE STRING "The UI's resolution: 480x320 or 800x480" FORCE)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(resolution)

if(MMU_BOARD STREQUAL "es3c50p")
    # 16 MB of flash holds two OTA slots and the tag library, and C++
    # exceptions are always on, so one generated sdkconfig serves the image
    # with or without the tag writer.
    set(IDF_TARGET "esp32s3" CACHE STRING "IDF Build Target")
    if(NOT DEFINED SDKCONFIG)
        set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.es3c50p")
    endif()
    if(NOT DEFINED SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.es3c50p")
    endif()
elseif(MMU_SPOOL_TAG_WRITER)
    # The tag writer needs its tag-library partition (one app slot and no OTA,
    # partitions_tagwriter.csv) and C++ exceptions, so it keeps its own
    # generated sdkconfig: switching the option needs no manual delete.
    if(NOT DEFINED SDKCONFIG)
        set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.tagwriter")
    endif()
    if(NOT DEFINED SDKCONFIG_DEFAULTS)
        set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.tagwriter")
    endif()
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Every component sees it, LVGL included: lv_conf.h enables the few widgets
# and the font the tag writer's pages use only in these builds.
if(MMU_SPOOL_TAG_WRITER)
    idf_build_set_property(COMPILE_DEFINITIONS "MMU_FEATURE_TAG_WRITER=1" APPEND)
endif()
string(TOUPPER "${MMU_BOARD}" mmu_board_upper)
idf_build_set_property(COMPILE_DEFINITIONS "MMU_BOARD_${mmu_board_upper}=1" APPEND)
foreach(def IN LISTS MMU_UI_DEFINES)
    idf_build_set_property(COMPILE_DEFINITIONS "${def}" APPEND)
endforeach()
# The component manager records the chip in its lock file: each chip keeps its
# own, so building one board does not rewrite the other's.
if(MMU_BOARD STREQUAL "es3c50p")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.esp32s3")
endif()

project(openams_ui_esp32)

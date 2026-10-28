# The portable app (src/model, src/ui, src/app) plus this platform's own
# hardware drivers (display, touch, tick, action sink) and app_main.
#
# -DMMU_LANGUAGES picks what the image carries (default: latin, the languages a
# device is likely to want): every string table and every CJK face of that set,
# and nothing else. cmake/languages.cmake is shared with the simulator and the
# wasm core; see the README.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
include(languages)
include(resolution)

file(GLOB_RECURSE MMU_APP_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../src/*.c"
)
mmu_languages_filter_sources(MMU_APP_SOURCES ${MMU_APP_SOURCES})
mmu_resolution_filter_sources(MMU_APP_SOURCES ${MMU_APP_SOURCES})

# The spool tag writer's menu screen (platform/esp32/CMakeLists.txt adds its
# component when MMU_SPOOL_TAG_WRITER is on; see the optional requirement below).
set(MMU_TAG_WRITER_SOURCES)
if(MMU_SPOOL_TAG_WRITER)
    set(MMU_TAG_WRITER_SOURCES "screen_tag_writer.c")
endif()

# The board's own panel and touch drivers (main/board.h); both implement
# display.h and touch.h. Only the resistive panel needs the calibration screen.
if(MMU_BOARD STREQUAL "es3c50p")
    set(MMU_BOARD_SOURCES "display_rgb.c" "touch_gt911.c")
else()
    set(MMU_BOARD_SOURCES "display.c" "touch.c" "touch_calib_store.c" "touch_calib_ui.c")
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "backlight.c"
        ${MMU_BOARD_SOURCES}
        "display_cfg_store.c"
        "display_power.c"
        "net_config.c"
        "net_console.c"
        "net_link.c"
        "net_wifi.c"
        "ota.c"
        "actions_esp32.c"
        ${MMU_TAG_WRITER_SOURCES}
        ${MMU_APP_SOURCES}
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../../src"
    LDFRAGMENTS
        "lvgl_fast.lf"
    REQUIRES
        lvgl
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_gpio
        esp_driver_i2c
        esp_lcd
        esp_driver_ledc
        esp_driver_uart
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        app_update
        esp_https_ota
        esp_http_client
        mbedtls
        log
)

# Which languages this image carries (MMU_LANG_<CODE>), which guards the string
# tables (src/view/strings_table.gen.h) and the CJK face list
# (src/ui/ui_fonts_lang.c). The pseudo-locale (qps) is for tests and layout
# review on the host; the device image leaves it out (owner, 2026-09-29, to fit
# eleven languages).
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ${MMU_LANGUAGE_DEFINES}
    VIEW_NO_PSEUDO=1)

# sdkconfig is generated once from sdkconfig.defaults and then wins over it, so
# a checkout that predates the QIO 80 MHz flash settings keeps building with
# DIO 40 MHz, and every screen draws about twice as slowly (2026-10-02). Say
# so: delete platform/esp32/sdkconfig and build again to pick up the defaults.
if(NOT CONFIG_ESPTOOLPY_FLASHMODE_QIO OR NOT CONFIG_ESPTOOLPY_FLASHFREQ_80M)
    message(WARNING "sdkconfig predates sdkconfig.defaults' QIO 80 MHz flash "
                    "settings: delete platform/esp32/sdkconfig and build again")
endif()

# The dependency resolution pass does not see project options, so the tag
# writer is an optional requirement: linked when its component is in the build.
idf_component_optional_requires(PRIVATE tag_writer)

# The ES3C50P's one layout carries the OTA slots and the tag library, and its
# sdkconfig always has C++ exceptions (sdkconfig.defaults.es3c50p).
if(MMU_BOARD STREQUAL "es3c50p")
    if(NOT CONFIG_IDF_TARGET_ESP32S3
       OR NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_es3c50p.csv")
        message(FATAL_ERROR "MMU_BOARD is es3c50p but ${SDKCONFIG} was generated for another "
                            "board: delete that file (or pass its own -DSDKCONFIG=...) and "
                            "build again")
    endif()
elseif(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_es3c50p.csv")
    message(FATAL_ERROR "MMU_BOARD is ${MMU_BOARD} but ${SDKCONFIG} was generated for the "
                        "ES3C50P: delete that file and build again")
# An image with the tag writer needs its partition table (the tag library) and
# C++ exceptions, one without it the two OTA slots. Each has its own generated
# sdkconfig (sdkconfig.tagwriter / sdkconfig), so this only trips when a build
# names an sdkconfig of the other kind with -D SDKCONFIG=...
elseif(MMU_SPOOL_TAG_WRITER)
    if(NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_tagwriter.csv"
       OR NOT CONFIG_COMPILER_CXX_EXCEPTIONS)
        message(FATAL_ERROR "MMU_SPOOL_TAG_WRITER is on but ${SDKCONFIG} was generated "
                            "without it: delete that file (or pass "
                            "-DMMU_SPOOL_TAG_WRITER=OFF) and build again")
    endif()
elseif(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "partitions_tagwriter.csv")
    message(FATAL_ERROR "MMU_SPOOL_TAG_WRITER is off but ${SDKCONFIG} was generated with "
                        "it: delete that file and build again")
endif()

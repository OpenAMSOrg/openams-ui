# The portable app (src/model, src/ui, src/app) plus this platform's own
# hardware drivers (display, touch, tick, action sink) and app_main.
#
# -DMMU_LANGUAGES picks what the image carries (default: latin, the languages a
# device is likely to want): every string table and every CJK face of that set,
# and nothing else. cmake/languages.cmake is shared with the simulator and the
# wasm core; see the README.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../cmake")
include(languages)

file(GLOB_RECURSE MMU_APP_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../src/*.c"
)
mmu_languages_filter_sources(MMU_APP_SOURCES ${MMU_APP_SOURCES})

idf_component_register(
    SRCS
        "app_main.c"
        "display.c"
        "display_cfg_store.c"
        "display_power.c"
        "net_config.c"
        "net_console.c"
        "net_link.c"
        "net_wifi.c"
        "ota.c"
        "touch.c"
        "touch_calib_store.c"
        "touch_calib_ui.c"
        "actions_esp32.c"
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

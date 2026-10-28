# The spool tag writer (include/tag_writer.h), an ESP32-only component of the
# display. platform/esp32/CMakeLists.txt adds this directory when
# MMU_SPOOL_TAG_WRITER is on (the default), and MMU_TAG_READER picks the
# reader: PN5180 (default) or MFRC522. See docs/guide/tag-writer.md.

# Every build: the screen, console, reader task and the Bambu flow, which both
# readers drive through the same interface (src/reader.h).
set(tw_sources
    "src/tag_writer.cpp"
    "src/ui.cpp"
    "src/console.cpp"
    "src/tag_service.cpp"
    "src/tag_ops.cpp"
    "src/spool_store.cpp"
    "src/filament_view.cpp"
    "src/bambu_view.cpp"
    "src/bambu_format.cpp"
    "src/sha256.cpp"
    "src/tag.cpp"
)

if(MMU_TAG_READER STREQUAL "MFRC522")
    list(APPEND tw_sources "src/mfrc522.cpp")
    set(tw_reader_define TW_READER_MFRC522=1)
else()
    # The PN5180 also reads NFC Forum Type 2 / Type 5 tags, so it gets the
    # universal path: the protocol layers and every format codec.
    list(APPEND tw_sources
        "src/pn5180_core.cpp"
        "src/pn5180_magic.cpp"
        "src/pn5180_idf_spi.cpp"
        "src/tag_reader.cpp"
        "src/iso14443a_type2.cpp"
        "src/iso15693.cpp"
        "src/ndef.cpp"
        "src/cbor.cpp"
        "src/aes.cpp"
        "src/formatcodec.cpp"
        "src/bambu_codec.cpp"
        "src/openprinttag.cpp"
        "src/openspool.cpp"
        "src/spoolman.cpp"
        "src/spoolease.cpp"
        "src/opentag3d.cpp"
        "src/tigertag.cpp"
        "src/anycubic.cpp"
        "src/elegoo.cpp"
        "src/creality.cpp"
        "src/snapmaker.cpp"
        "src/qidi.cpp"
    )
    set(tw_reader_define TW_READER_PN5180=1)
endif()

# The tag library (src/taglib.h): served by OpenAMS on the printer host, or
# in the board's own flash partition (MMU_TAG_LIBRARY=board, 5 inch only).
if(MMU_TAG_LIBRARY STREQUAL "board")
    list(APPEND tw_sources "src/taglib.cpp")
    set(tw_library_define TW_TAGLIB_BOARD=1)
else()
    list(APPEND tw_sources "src/taglib_remote.cpp")
    set(tw_library_define TW_TAGLIB_REMOTE=1)
endif()

idf_component_register(
    SRCS ${tw_sources}
    INCLUDE_DIRS "include"
    # ../../../src: the display's ui/ui_scale.h, the grid the pages are laid out on.
    PRIV_INCLUDE_DIRS "src" "../../../src"
    REQUIRES lvgl
    PRIV_REQUIRES
        esp_driver_spi
        esp_driver_gpio
        esp_driver_uart
        esp_http_client
        esp_app_format
        esp_partition
        esp_system
        esp_timer
        nvs_flash
        log
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${tw_reader_define} ${tw_library_define})

# Nothing here is drawing-bound (the display's -O2 is for LVGL rendering), so
# this component is built for size.
target_compile_options(${COMPONENT_LIB} PRIVATE -Os)

# The Bambu codec truncates strings to fixed tag-field lengths on purpose.
set_source_files_properties("src/bambu_format.cpp"
    PROPERTIES COMPILE_OPTIONS "-Wno-stringop-truncation")

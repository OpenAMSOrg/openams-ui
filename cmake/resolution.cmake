# The UI's resolution: -DMMU_UI_RESOLUTION=480x320 (the default) or 800x480.
# Shared by the simulator, the wasm build and the ESP32 image (whose board sets
# it: the ES3C50P is 800x480).
#
# Every screen is laid out on a 480x320 grid (docs/VISUAL_SPEC_480.md). The
# 800x480 build draws that grid at 1.5x (UI_PX(), src/ui/ui_theme.h) with
# fonts generated at 1.5x (tools/gen_fonts.py --set 800x480), and the screens
# spend the extra width themselves (docs/VISUAL_SPEC_800.md).
#
#   MMU_UI_DEFINES                 the compile definitions that select it
#   mmu_resolution_filter_sources  keeps one font set out of a source list

set(MMU_UI_RESOLUTION "480x320" CACHE STRING "The UI's resolution: 480x320 or 800x480")
set_property(CACHE MMU_UI_RESOLUTION PROPERTY STRINGS 480x320 800x480)
if(NOT MMU_UI_RESOLUTION MATCHES "^(480x320|800x480)$")
    message(FATAL_ERROR "MMU_UI_RESOLUTION is \"${MMU_UI_RESOLUTION}\"; it takes 480x320 or 800x480")
endif()

if(MMU_UI_RESOLUTION STREQUAL "800x480")
    set(MMU_UI_DEFINES "MMU_UI_800X480=1")
else()
    set(MMU_UI_DEFINES "")
endif()

# ${out} <- ${sources} with only this resolution's faces: the 480x320 set is
# src/ui/fonts/*.c, the 800x480 set src/ui/fonts/800x480/*.c, under the same
# symbols. Headers are shared.
function(mmu_resolution_filter_sources out)
    set(sources ${ARGN})
    if(MMU_UI_RESOLUTION STREQUAL "800x480")
        list(FILTER sources EXCLUDE REGEX "/src/ui/fonts/ui_font_[^/]*\\.c$")
    else()
        list(FILTER sources EXCLUDE REGEX "/src/ui/fonts/800x480/")
    endif()
    set(${out} "${sources}" PARENT_SCOPE)
endfunction()

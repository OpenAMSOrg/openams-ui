/**
 * @file sim.h
 * The pieces of the SDL simulator's capture/pump machinery that simulator-only
 * tooling (the component gallery) reuses. Everything else in main.c stays
 * private to it.
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>

/** Snapshot the active screen (blending lv_layer_top() over it when @p
 * with_top_layer) and write it as a 24-bit BMP to @p path. Returns false when
 * the snapshot or the file could not be taken. */
bool sim_capture_bmp(const char *path, bool with_top_layer);

/** Advance the simulator clock by @p n steps of 33 ms, running LVGL timers,
 * exactly as the --smoke/--screenshots walk does. */
void sim_pump(int n);

#endif /* SIM_H */

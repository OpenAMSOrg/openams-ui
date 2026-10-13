// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file ui.h
 * The tag writer's pages, hosted in one of the display's screens.
 */
#pragma once

#include "lvgl.h"

namespace ui {

/** Builds the tag writer into @p root, a screen object the display created
 * and loads, and holds the reader while it is open. @p on_exit runs (on the
 * LVGL task) when the user leaves; the display then deletes @p root, which
 * stops the pages' timer and lets go of the reader. LVGL task only. */
void open(lv_obj_t *root, void (*on_exit)(void));

}  // namespace ui

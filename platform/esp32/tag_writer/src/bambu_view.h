// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// Based on openams (GPLv3) and OpenRFID (GPLv3).
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file bambu_view.h
 * A Bambu-layout tag (bambu::TagInfo) as the universal DecodeResult, so the
 * tag view and the console show it like any other format. Used by the Bambu
 * codec (PN5180 builds) and by the plain MIFARE read (every build).
 */
#pragma once

#include "bambu_format.h"
#include "formatcodec.h"

namespace bambu_view {

/** Fills @p out from @p info. When the read failed (@p ok false) the result is
 * unrecognized and carries @p error; a tag whose material fields are all empty
 * reads as blank. */
void to_result(const bambu::TagInfo &info, bool ok, const char *error, DecodeResult &out);

}  // namespace bambu_view

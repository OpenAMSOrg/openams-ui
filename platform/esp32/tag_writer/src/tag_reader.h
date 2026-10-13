// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Universal tag read path: detect + classify a tag of any supported
// technology, read its whole memory (MIFARE Classic key search included),
// and decode it across every registered format codec.
//
// This is the firmware port of klipper_rf reader.py (TagReader): it sits on
// the firmware's own Pn5180 driver and the ported protocol layers
// (iso14443a_type2 / iso15693), and feeds the decoded Tag to decode_tag().
// The existing tag_ops / bambu_format path still handles Bambu-specific
// read/write/clone; this path recognizes every OTHER format for parity.
#pragma once

#include <vector>

#include "formatcodec.h"
#include "pn5180.h"
#include "tag.h"

namespace tagrd {

// Build the codec registry once from the app's configured keys. Owns the
// codecs for the process lifetime.
void begin(const CodecOptions &options);
const std::vector<FormatCodec *> &codecs();
// Frees the registry (the reader task leaves); begin() builds it again.
void end();

// Turn the field on and look for a tag with each protocol (ISO15693 then
// ISO14443A). Fills `out` with identity + geometry (no memory read yet) and
// `out_uid` with the ISO14443A card UID for MIFARE auth. False if none.
bool detect(Pn5180 &chip, Tag &out, CardUid &out_uid);

// Read the whole tag memory into tag.memory/readable. Type 2: every page.
// Type 5: every block. MIFARE Classic: codec key sources first, then the
// default keys. Returns false only for an unsupported tag type; a partial
// read still returns true.
bool read_memory(Pn5180 &chip, Tag &tag, const CardUid &uid);

// Decode a read tag across all registered codecs (wraps decode_tag()).
DecodeResult decode(const Tag &tag);

// Encode `fields` in the codec named `format` into an already-detected and
// read `tag`, then write the changed blocks to the physical tag and verify.
// The caller must have populated tag.memory via detect()+read_memory() first.
// Only the user-area blocks that actually change are written (UID/lock/CC
// pages are never touched). Supports the NDEF formats on NFC Forum Type 2
// (NTAG/Type2) and Type 5 (ISO15693) tags. Returns false with `err` set on
// any failure: unknown format, the format cannot encode this tag, an encode
// error, or a write/verify error. `written` gets the block count on success.
bool encode_and_write(Pn5180 &chip, Tag &tag, const CardUid &uid,
                      const std::string &format, const Filament &fields,
                      std::string &err, int *written = nullptr);

}  // namespace tagrd

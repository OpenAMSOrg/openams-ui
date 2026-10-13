// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

// Card-level operations: classify a tag, read/dump it, write a spool to it,
// or reset it to a blank card. Not thread-safe: tag_service.cpp owns the
// reader and calls these from its own task.
#pragma once

#include <stdint.h>

#include "bambu_format.h"
#include "reader.h"

namespace tagops {

enum Kind : uint8_t {
    NONE,          // nothing in the field
    UNSUPPORTED,   // not MIFARE Classic (e.g. NTAG, DESFire)
    BLANK,         // factory keys: writable
    CUSTOM,        // Bambu layout with derived keys, writable (ours)
    GENUINE,       // derived keys and an RSA signature: a real Bambu tag
    LOCKED,        // derived keys but read-only access bits, no signature
    UNKNOWN_KEYS,  // MIFARE Classic with keys we do not know
};

struct Card {
    CardUid uid;
    Kind kind;
    uint8_t keys[bambu::SECTORS][bambu::KEY_SIZE];
};

struct Result {
    bool ok;
    Reader::Status status;
    uint8_t sector;  // where it failed (0xFF = not sector-specific)
    const char *message;
};

typedef void (*Progress)(uint8_t done, uint8_t total);
typedef bool (*Cancelled)();

void begin(Reader &reader);
void set_master_key(const uint8_t key[bambu::MASTER_KEY_SIZE]);
bool has_master_key();
// First four bytes of SHA-256(master key): names the key without revealing it.
void key_fingerprint(char out[9]);

void field(bool on);
// Selects whatever is in the field and classifies it. If `known` is the same
// card, its classification is reused instead of re-authenticating. Leaves the
// card halted.
bool detect(Card &card, const Card *known = nullptr);

Result read(Card &card, bambu::TagInfo &info);
// Reads blocks 0..63 into image (1024 bytes); ok[block] says which worked.
Result dump(Card &card, uint8_t *image, bool *ok);
Result write(Card &card, const bambu::Spool &spool, const char *date, Progress progress);
Result erase(Card &card, Progress progress);

// --- cloning genuine tags onto "magic" (UID-writable) tags ---------------

// Validates a 1024-byte MIFARE 1K image as a Bambu tag: block-0 BCC, derived
// sector keys present in the trailers. Fills `info` with the decoded spool.
// A rejected image is never written to a tag.
bool image_is_valid_bambu(const uint8_t image[1024], bambu::TagInfo &info, const char **why);

// Which magic type the tag on the reader is (non-destructive). `card` must
// already be selected (detect()).
Reader::Magic magic_detect(Card &card);

// Reads a genuine tag whole into `image` with the derived keys, for the
// library. ok[block] flags any block that would not read.
Result capture(Card &card, uint8_t image[1024], bool ok[64]);

// Writes a genuine `image` (block 0 / UID included) onto the magic tag now on
// the reader, then re-selects by the image's UID and verifies every block.
// `card` is the magic tag as detect() found it.
Result clone(Card &card, const uint8_t image[1024], Progress progress);

const char *kind_name(Kind kind);
bool is_writable(Kind kind);
void format_uid(const CardUid &uid, char *out);  // needs 2*size+1 bytes
bool same_uid(const CardUid &a, const CardUid &b);

}  // namespace tagops

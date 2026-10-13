// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file reader.h
 * The RFID reader this image drives, picked at build time (-DMMU_TAG_READER):
 *
 *   PN5180  (default) also reads NFC Forum Type 2 (NTAG) and Type 5 (ICODE)
 *           tags and every filament format the format codecs know, and writes
 *           the writable ones;
 *   MFRC522 MIFARE Classic only: the Bambu read / write / reset / clone flow.
 *
 * Both drivers expose the same interface for everything tag_ops and
 * tag_service use (select, authenticate, read/write_block, the magic-tag
 * backdoors, status names), so that code is written against `Reader`. The
 * universal read/write path (tag_reader, iso14443a_type2, iso15693 and the
 * format codecs) needs the PN5180 and is built only with it.
 */
#pragma once

#if defined(TW_READER_MFRC522)
#include "mfrc522.h"
typedef Mfrc522 Reader;
#define TW_READER_NAME "MFRC522"
#else
#include "pn5180.h"
typedef Pn5180 Reader;
#define TW_READER_NAME "PN5180"
/** Defined when this image has the universal (every format) read/write path. */
#define TW_READER_UNIVERSAL 1
#endif

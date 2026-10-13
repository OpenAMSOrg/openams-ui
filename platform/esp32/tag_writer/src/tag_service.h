// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file tag_service.h
 * The reader's own task. It polls for tags while idle and runs one job at a
 * time (read, write, erase, dump) for whoever asked, the tag writer screen or
 * the serial console. Everything else talks to it through snapshots and
 * requests.
 *
 * The task only exists while someone holds the reader (acquire/release): the
 * display spends its heap on the printer link the rest of the time, and the
 * reader's field stays off.
 */
#pragma once

#include <stdint.h>

#include "bambu_format.h"
#include "formatcodec.h"
#include "reader.h"
#include "tag_ops.h"

namespace tagsvc {

enum Op : uint8_t {
    OP_NONE, OP_READ, OP_WRITE, OP_ERASE, OP_DUMP, OP_SELFTEST,
    OP_MAGIC,    // identify a magic tag (gen1a / gen2 / gen3 / normal)
    OP_CAPTURE,  // read a genuine tag into the image buffer (for the library)
    OP_CLONE,    // write the image buffer onto a magic tag
    OP_DECODE,   // universal read: detect any tag type, decode every format
    OP_WRITE_UNIVERSAL,  // encode a filament in a chosen format and write it
};
enum Phase : uint8_t { IDLE, WAITING, RUNNING, DONE };
enum Owner : uint8_t { OWNER_UI, OWNER_CONSOLE };

struct Snapshot {
    bool reader_ok;
    uint8_t version;
    bool irq;
    bool key_set;
    char key_fingerprint[9];

    bool present;        // what idle polling sees on the reader
    tagops::Card card;

    Op op;               // the current or last job
    Owner owner;
    Phase phase;
    uint8_t done, total;
    tagops::Card job_card;
    tagops::Result result;
    bambu::TagInfo info;  // after a read
    bool info_ok;

    Reader::Magic magic;  // after OP_MAGIC
    bool image_valid;      // the image buffer holds a validated Bambu tag
    bambu::TagInfo image_info;  // what that image decodes to

    uint32_t seq;        // changes whenever anything above changes
};

/** Remembers the wiring and the Bambu master key; touches no hardware. Once,
 * at boot. */
void init(const Reader::Pins &pins, const uint8_t master_key[bambu::MASTER_KEY_SIZE]);
/** Hold the reader. The first holder brings the reader up and starts its task;
 * returns true when this call started it (no poll has run yet). */
bool acquire();
/** Let go of the reader. When the last holder lets go, a job still waiting
 * for a tag is cancelled and the task powers the field down and exits (its
 * stack goes back to the heap). Never blocks. */
void release();
/** True while the task and its session are up (a holder may still find them
 * missing when the heap was too short to start them). */
bool ready();
/** The reader task's least free stack so far, in bytes (0 when it is not running). */
unsigned stack_left();
/** After acquire() started the task: wait up to timeout_ms for its first poll,
 * so a snapshot then says whether a tag is on the reader. */
void wait_first_poll(uint32_t timeout_ms);
void snapshot(Snapshot &out);

// Queues a job; it waits up to wait_ms for a tag. False if one is running.
bool submit(Op op, Owner owner, const bambu::Spool *spool, const char *date, uint32_t wait_ms);
// Abandons this owner's job if it is still waiting for a tag or running.
void cancel(Owner owner);
// Done with a finished job's result.
void acknowledge();
// After a finished OP_DUMP: 1024 bytes and 64 per-block flags.
void dump_result(uint8_t *image, bool *ok);

// The image buffer (shared by OP_CAPTURE, OP_CLONE and the console).
// load_image validates a 1024-byte image before storing it; false (with
// *why set) if it is not a Bambu tag. get_image copies it out.
bool load_image(const uint8_t image[1024], const char **why);
void get_image(uint8_t image[1024], bool *valid);

void set_master_key(const uint8_t key[bambu::MASTER_KEY_SIZE]);

#if defined(TW_READER_UNIVERSAL)
// After a finished OP_DECODE: the universal decode result and the detected
// tag type (nfc.tag constant, e.g. "ntag215"). `ok` is false if no tag was
// found. Copies the result out under the service lock.
void get_decode(DecodeResult &out, char tag_type[24], bool *ok);

// Stage a universal write: encode `fields` in the codec named `format` and
// write it to the next tag an OP_WRITE_UNIVERSAL job sees. Call before submit.
void set_write_request(const std::string &format, const Filament &fields);
// After a finished OP_WRITE_UNIVERSAL: whether it succeeded, the block count
// written, and a message (error text, or "wrote N blocks").
void get_write_result(bool *ok, int *written, char *msg, size_t msg_len);
#endif

}  // namespace tagsvc

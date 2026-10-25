// Copyright (C) 2026 JR Lomas <lomas.jr@gmail.com>
// This file may be distributed under the terms of the GNU GPLv3 license.

/**
 * @file tag_service.cpp
 * The reader task: tag presence polling and one job at a time. See
 * tag_service.h.
 */
#include "tag_service.h"

#include <new>
#include <string.h>

#if defined(TW_READER_UNIVERSAL)
#include "tag_reader.h"
#endif
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace tagsvc {

static const char *TAG = "tagsvc";

static Reader reader;
static SemaphoreHandle_t lock;
static Snapshot state;

// Lifecycle (acquire/release), guarded by `life`. The task decides to exit
// under the same lock, so an acquire() either finds it alive and keeps it, or
// finds it gone and starts a new one: never two at once.
static SemaphoreHandle_t life;
static Reader::Pins reader_pins;
static uint8_t boot_key[bambu::MASTER_KEY_SIZE];
static int holders;
static bool task_alive;
static bool stop_requested;
static bool reader_begun;
static volatile bool polled;
static int64_t linger_until;  // after the last release(): when the task leaves
static bool lingering;
static bool no_linger;        // the last release() asked to leave at once
static TaskHandle_t task_handle;

// After the last holder lets go the task waits this long before it leaves, so
// back-to-back console commands (loadimage, then clone) share one session and
// its clone image. The field is off while it waits.
#define LINGER_MS 3000

// The reader task's stack, its largest single allocation.
#define TASK_STACK 6144
#if defined(TW_READER_UNIVERSAL)
static bool codecs_built;
#endif

// Request from another task, guarded by `lock`.
static bool job_requested;
static bambu::Spool job_spool;
static char job_date[17];
static uint32_t job_wait_ms;
static bool cancel_requested;
static bool key_pending;
static uint8_t pending_key[bambu::MASTER_KEY_SIZE];

// Everything a session needs beyond the snapshot, on the heap while the task
// runs (acquire() to the task's exit) instead of in static RAM for good: the
// display needs that heap for its printer link the rest of the time. Guarded
// by `lock`; nullptr when no session is open.
struct Session {
    uint8_t image[1024];      // the clone image buffer: a captured or PC-loaded genuine tag
    bool image_valid;
    uint8_t dump[1024];       // after OP_DUMP
    bool dump_ok[64];
    uint8_t scratch[1024];    // a capture's or a clone's working copy (one job at a time)
#if defined(TW_READER_UNIVERSAL)
    DecodeResult decode;      // after OP_DECODE
    char decode_type[24];
    bool decode_ok;
    std::string write_format; // the pending OP_WRITE_UNIVERSAL request, and its result
    Filament write_fields;
    bool write_ok;
    int write_written;
    char write_msg[96];
#endif
};
static Session *sess;


static int64_t now_ms() { return esp_timer_get_time() / 1000; }

static void touch_state() { state.seq++; }

void snapshot(Snapshot &out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    out = state;
    xSemaphoreGive(lock);
}

bool submit(Op op, Owner owner, const bambu::Spool *spool, const char *date, uint32_t wait_ms) {
    xSemaphoreTake(lock, portMAX_DELAY);
    // Busy while a job is queued or running, or while another owner's finished
    // result is still unacknowledged (submitting would clobber it).
    bool busy = job_requested || state.phase == WAITING || state.phase == RUNNING ||
                (state.phase == DONE && state.owner != owner);
    if (!busy) {
        state.op = op;
        state.owner = owner;
        state.phase = WAITING;
        state.done = state.total = 0;
        state.info_ok = false;
        memset(&state.result, 0, sizeof(state.result));
        if (spool)
            job_spool = *spool;
        strncpy(job_date, date ? date : "", sizeof(job_date) - 1);
        job_date[sizeof(job_date) - 1] = 0;
        job_wait_ms = wait_ms;
        cancel_requested = false;
        job_requested = true;
        touch_state();
    }
    xSemaphoreGive(lock);
    return !busy;
}

void cancel(Owner owner) {
    xSemaphoreTake(lock, portMAX_DELAY);
    // Only the owner can cancel its own job, so one owner closing its screen
    // never aborts the other's in-flight job (which would hang its waiter).
    if ((state.phase == WAITING || state.phase == RUNNING) && state.owner == owner)
        cancel_requested = true;
    xSemaphoreGive(lock);
}

void acknowledge() {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (state.phase == DONE) {
        state.phase = IDLE;
        touch_state();
    }
    xSemaphoreGive(lock);
}

void dump_result(uint8_t *image, bool *ok) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (sess) {
        memcpy(image, sess->dump, sizeof(sess->dump));
        memcpy(ok, sess->dump_ok, sizeof(sess->dump_ok));
    } else {
        memset(image, 0, 1024);
        memset(ok, 0, 64 * sizeof(bool));
    }
    xSemaphoreGive(lock);
}

bool load_image(const uint8_t image[1024], const char **why) {
    bambu::TagInfo info;
    if (!tagops::image_is_valid_bambu(image, info, why))
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!sess) {  // only while the reader is held (the screen, a console command)
        xSemaphoreGive(lock);
        if (why)
            *why = "the reader is not in use";
        return false;
    }
    memcpy(sess->image, image, sizeof(sess->image));
    sess->image_valid = true;
    state.image_valid = true;
    state.image_info = info;
    touch_state();
    xSemaphoreGive(lock);
    return true;
}

void get_image(uint8_t image[1024], bool *valid) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (sess)
        memcpy(image, sess->image, sizeof(sess->image));
    else
        memset(image, 0, 1024);
    if (valid)
        *valid = sess && sess->image_valid;
    xSemaphoreGive(lock);
}

// Takes the key into tag_ops and the snapshot. Caller holds `lock`, and the
// task is either not running or is the caller.
static void apply_key(const uint8_t key[bambu::MASTER_KEY_SIZE]) {
    tagops::set_master_key(key);
    state.key_set = tagops::has_master_key();
    tagops::key_fingerprint(state.key_fingerprint);
    touch_state();
}

#if defined(TW_READER_UNIVERSAL)
// (Re)builds the format-codec registry. The Bambu codec gets the master key
// (it stays on the device); creality/snapmaker stay inert until their vendor
// key is set; the key-free formats (NDEF, Type 2, QIDI) work at once. Only
// while no decode can be running: from acquire() before the task starts, or
// from the task itself.
static void build_codecs(const uint8_t key[bambu::MASTER_KEY_SIZE]) {
    CodecOptions opts;
    opts.values["bambu_key"] =
        std::string(reinterpret_cast<const char *>(key), bambu::MASTER_KEY_SIZE);
    tagrd::begin(opts);
    codecs_built = true;
}
#endif

void set_master_key(const uint8_t key[bambu::MASTER_KEY_SIZE]) {
    xSemaphoreTake(life, portMAX_DELAY);
    memcpy(boot_key, key, sizeof(boot_key));
    xSemaphoreTake(lock, portMAX_DELAY);
    if (task_alive) {
        // A job may be deriving keys right now: the task takes it between jobs.
        memcpy(pending_key, key, sizeof(pending_key));
        key_pending = true;
    } else {
        apply_key(key);
#if defined(TW_READER_UNIVERSAL)
        codecs_built = false;  // rebuilt with this key by the next acquire()
#endif
    }
    xSemaphoreGive(lock);
    xSemaphoreGive(life);
}

#if defined(TW_READER_UNIVERSAL)
void get_decode(DecodeResult &out, char tag_type[24], bool *ok) {
    xSemaphoreTake(lock, portMAX_DELAY);
    out = sess ? sess->decode : DecodeResult();
    snprintf(tag_type, 24, "%s", sess ? sess->decode_type : "");
    if (ok)
        *ok = sess && sess->decode_ok;
    xSemaphoreGive(lock);
}

void set_write_request(const std::string &format, const Filament &fields) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (sess) {
        sess->write_format = format;
        sess->write_fields = fields;
    }
    xSemaphoreGive(lock);
}

void get_write_result(bool *ok, int *written, char *msg, size_t msg_len) {
    xSemaphoreTake(lock, portMAX_DELAY);
    if (ok) *ok = sess && sess->write_ok;
    if (written) *written = sess ? sess->write_written : 0;
    if (msg && msg_len)
        snprintf(msg, msg_len, "%s", sess ? sess->write_msg : "the reader is not in use");
    xSemaphoreGive(lock);
}
#endif

static void progress(uint8_t done, uint8_t total) {
    esp_task_wdt_reset();  // a write or clone reports every sector
    xSemaphoreTake(lock, portMAX_DELAY);
    state.done = done;
    state.total = total;
    touch_state();
    xSemaphoreGive(lock);
}

static void finish_job(const tagops::Card &card, const tagops::Result &r,
                       const bambu::TagInfo *info) {
    xSemaphoreTake(lock, portMAX_DELAY);
    state.job_card = card;
    state.result = r;
    if (info) {
        state.info = *info;
        state.info_ok = r.ok;
    }
    state.phase = DONE;
    // The job may have changed the tag (written, erased): show it as it is now.
    state.present = card.kind != tagops::NONE;
    state.card = card;
    touch_state();
    xSemaphoreGive(lock);
}

#if defined(TW_READER_UNIVERSAL)
// Universal read: poll with the protocol-agnostic reader, then read all of
// the tag's memory and decode it across every registered format codec.
static void run_decode() {
    int64_t deadline = now_ms() + job_wait_ms;
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        bool cancelled = cancel_requested;
        xSemaphoreGive(lock);
        if (cancelled) {
            reader.antenna(false);
            xSemaphoreTake(lock, portMAX_DELAY);
            state.phase = IDLE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        esp_task_wdt_reset();

        Tag tag;
        CardUid uid;
        if (tagrd::detect(reader, tag, uid)) {
            tagrd::read_memory(reader, tag, uid);
            DecodeResult dr = tagrd::decode(tag);
            reader.antenna(false);

            tagops::Card card;
            memset(&card, 0, sizeof(card));
            card.uid = uid;
            card.kind = tagops::UNSUPPORTED;  // neutral; see the session for data

            xSemaphoreTake(lock, portMAX_DELAY);
            sess->decode = dr;
            sess->decode_ok = true;
            snprintf(sess->decode_type, sizeof(sess->decode_type), "%s",
                     tag_type_to_string(tag.tag_type));
            state.job_card = card;
            state.result = {dr.recognized, Reader::OK, 0xFF,
                            dr.recognized ? "recognized" : "unrecognized"};
            state.info_ok = false;
            state.present = true;
            state.card = card;
            state.phase = DONE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        if (now_ms() > deadline) {
            reader.antenna(false);
            xSemaphoreTake(lock, portMAX_DELAY);
            sess->decode = DecodeResult();
            sess->decode_ok = false;
            sess->decode_type[0] = 0;
            tagops::Card none;
            memset(&none, 0, sizeof(none));
            none.kind = tagops::NONE;
            state.job_card = none;
            state.result = {false, Reader::NO_CARD, 0xFF, "no tag on the reader"};
            state.present = false;
            state.phase = DONE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

// Universal write: detect + read a tag, then encode the staged filament in
// the chosen format and write back the changed blocks. Parity-path mirror of
// run_decode(); result goes to the session for the console/UI to report.
static void run_write_universal() {
    int64_t deadline = now_ms() + job_wait_ms;
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        bool cancelled = cancel_requested;
        xSemaphoreGive(lock);
        if (cancelled) {
            reader.antenna(false);
            xSemaphoreTake(lock, portMAX_DELAY);
            state.phase = IDLE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        esp_task_wdt_reset();

        Tag tag;
        CardUid uid;
        if (tagrd::detect(reader, tag, uid)) {
            tagrd::read_memory(reader, tag, uid);

            std::string fmt;
            Filament fields;
            xSemaphoreTake(lock, portMAX_DELAY);
            fmt = sess->write_format;
            fields = sess->write_fields;
            xSemaphoreGive(lock);

            std::string err;
            int written = 0;
            bool ok = tagrd::encode_and_write(reader, tag, uid, fmt, fields, err, &written);
            reader.antenna(false);

            tagops::Card card;
            memset(&card, 0, sizeof(card));
            card.uid = uid;
            card.kind = tagops::UNSUPPORTED;

            char msg[96];
            if (ok)
                snprintf(msg, sizeof(msg), "wrote %d block%s as %s", written,
                         written == 1 ? "" : "s", fmt.c_str());
            else
                snprintf(msg, sizeof(msg), "%s", err.c_str());

            xSemaphoreTake(lock, portMAX_DELAY);
            sess->write_ok = ok;
            sess->write_written = written;
            snprintf(sess->write_msg, sizeof(sess->write_msg), "%s", msg);
            state.job_card = card;
            state.result = {ok, Reader::OK, 0xFF, ok ? "written" : "write failed"};
            state.info_ok = false;
            state.present = true;
            state.card = card;
            state.phase = DONE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        if (now_ms() > deadline) {
            reader.antenna(false);
            xSemaphoreTake(lock, portMAX_DELAY);
            sess->write_ok = false;
            sess->write_written = 0;
            snprintf(sess->write_msg, sizeof(sess->write_msg), "%s", "no tag on the reader");
            tagops::Card none;
            memset(&none, 0, sizeof(none));
            none.kind = tagops::NONE;
            state.job_card = none;
            state.result = {false, Reader::NO_CARD, 0xFF, "no tag on the reader"};
            state.present = false;
            state.phase = DONE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}
#endif

static void run_job(Op op) {
    tagops::Card card;
    if (op == OP_SELFTEST) {  // no tag needed; prints its own report
        bool ok = reader.self_test();
        tagops::Result r = {ok, Reader::OK, 0xFF, ok ? "self-test passed" : "self-test failed"};
        card.kind = tagops::NONE;
        finish_job(card, r, nullptr);
        return;
    }
#if defined(TW_READER_UNIVERSAL)
    if (op == OP_DECODE) {
        run_decode();
        return;
    }
    if (op == OP_WRITE_UNIVERSAL) {
        run_write_universal();
        return;
    }
#endif
    int64_t deadline = now_ms() + job_wait_ms;
    tagops::field(true);
    for (;;) {
        xSemaphoreTake(lock, portMAX_DELAY);
        bool cancelled = cancel_requested;
        xSemaphoreGive(lock);
        if (cancelled) {
            tagops::field(false);
            xSemaphoreTake(lock, portMAX_DELAY);
            state.phase = IDLE;
            touch_state();
            xSemaphoreGive(lock);
            return;
        }
        esp_task_wdt_reset();
        if (tagops::detect(card))
            break;
        if (now_ms() > deadline) {
            tagops::field(false);
            tagops::Result r = {false, Reader::NO_CARD, 0xFF, "no tag on the reader"};
            card.kind = tagops::NONE;
            finish_job(card, r, nullptr);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    state.phase = RUNNING;
    state.job_card = card;
    touch_state();
    xSemaphoreGive(lock);

    tagops::Result r;
    bambu::TagInfo info;
    switch (op) {
    case OP_READ:
        r = tagops::read(card, info);
        finish_job(card, r, &info);
        break;
    case OP_WRITE:
        r = tagops::write(card, job_spool, job_date, progress);
        finish_job(card, r, nullptr);
        break;
    case OP_ERASE:
        r = tagops::erase(card, progress);
        finish_job(card, r, nullptr);
        break;
    case OP_DUMP: {
        // Read by dump_result() only once the job is DONE.
        r = tagops::dump(card, sess->dump, sess->dump_ok);
        finish_job(card, r, nullptr);
        break;
    }
    case OP_MAGIC: {
        Reader::Magic magic = tagops::magic_detect(card);
        xSemaphoreTake(lock, portMAX_DELAY);
        state.magic = magic;
        xSemaphoreGive(lock);
        r = {true, Reader::OK, 0xFF, Reader::magic_name(magic)};
        finish_job(card, r, nullptr);
        break;
    }
    case OP_CAPTURE: {
        bool ok[64];
        r = tagops::capture(card, sess->scratch, ok);
        if (r.ok) {
            const char *why;
            load_image(sess->scratch, &why);  // validates and decodes into image_info
        }
        finish_job(card, r, r.ok ? &state.image_info : nullptr);
        break;
    }
    case OP_CLONE: {
        bool valid = false;
        get_image(sess->scratch, &valid);
        if (!valid) {
            r = {false, Reader::OK, 0xFF, "no image loaded to clone"};
        } else {
            r = tagops::clone(card, sess->scratch, progress);
        }
        finish_job(card, r, nullptr);
        break;
    }
    default:
        break;
    }
    tagops::field(false);
}

static void poll_presence(tagops::Card &cached, bool &have_cached, uint8_t &misses) {
    tagops::field(true);
    tagops::Card fresh;
    bool found = tagops::detect(fresh, have_cached ? &cached : nullptr);
    tagops::field(false);

    xSemaphoreTake(lock, portMAX_DELAY);
    state.irq = reader.irq_level();
    if (found) {
        misses = 0;
        bool changed = !state.present || !tagops::same_uid(fresh.uid, state.card.uid) ||
                       fresh.kind != state.card.kind;
        cached = fresh;
        have_cached = true;
        state.present = true;
        state.card = fresh;
        if (changed)
            touch_state();
    } else if (state.present && ++misses >= 2) {
        have_cached = false;
        state.present = false;
        touch_state();
    }
    xSemaphoreGive(lock);
}

// After the last holder has let go the task lingers (field off, no polling)
// for LINGER_MS, then leaves for good. Decided under `life`, so a concurrent
// acquire() either keeps this task (it cleared the request first) or starts a
// fresh one after it. Returns true to leave; *idle while lingering.
static bool should_exit(bool *idle) {
    bool exit = false;
    bool start_linger = false;
    xSemaphoreTake(life, portMAX_DELAY);
    if (stop_requested) {
        if (!linger_until) {
            linger_until = now_ms() + (no_linger ? 0 : LINGER_MS);
            start_linger = true;
            reader.antenna(false);
        }
        if (now_ms() >= linger_until) {
            // The whole teardown happens here, under `life`, BEFORE task_alive
            // is cleared: otherwise an acquire() (LVGL task, life then lock)
            // could slip in after task_alive=false, build a fresh session, and
            // have this task then free the codecs and delete the new session.
#if defined(TW_READER_UNIVERSAL)
            tagrd::end();  // the codecs too; acquire() builds them again
            codecs_built = false;
#endif
            xSemaphoreTake(lock, portMAX_DELAY);
            state.present = false;
            delete sess;  // the session's buffers go back to the heap
            sess = nullptr;
            state.image_valid = false;
            touch_state();
            xSemaphoreGive(lock);

            exit = true;
            task_alive = false;
            task_handle = nullptr;
            stop_requested = false;
            no_linger = false;
            linger_until = 0;
        }
    } else {
        linger_until = 0;
    }
    lingering = stop_requested && !exit;
    *idle = lingering;
    xSemaphoreGive(life);

    if (start_linger) {
        xSemaphoreTake(lock, portMAX_DELAY);
        state.present = false;  // nobody is looking until the next acquire()
        touch_state();
        xSemaphoreGive(lock);
    }
    return exit;
}

static void task(void *) {
    tagops::Card cached;
    bool have_cached = false;
    uint8_t misses = 0;
    esp_task_wdt_add(nullptr);
    for (;;) {
        esp_task_wdt_reset();
        bool idle = false;
        if (should_exit(&idle))
            break;
        if (idle) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        if (key_pending) {
            key_pending = false;
            apply_key(pending_key);
#if defined(TW_READER_UNIVERSAL)
            build_codecs(pending_key);  // rebuild the codecs with the new Bambu key
#endif
            have_cached = false;
        }
        bool job = job_requested;
        Op op = state.op;
        job_requested = false;
        xSemaphoreGive(lock);

        // A job runs even with no reader (it ends at its deadline or errors):
        // dropping it would leave its phase stuck at WAITING and hang the
        // waiter. The ported layers throw on a hung reader or a hostile tag;
        // a throw must become a failed job, never a std::terminate reboot.
        if (job) {
            bool failed = false;
            try {
                run_job(op);
            } catch (const std::exception &e) {
                ESP_LOGE(TAG, "job error: %s", e.what());
                failed = true;
            } catch (...) {
                ESP_LOGE(TAG, "job error (unknown)");
                failed = true;
            }
            if (failed) {
                reader.antenna(false);
                xSemaphoreTake(lock, portMAX_DELAY);
#if defined(TW_READER_UNIVERSAL)
                if (sess) {
                    sess->decode_ok = false;
                    sess->write_ok = false;
                    snprintf(sess->write_msg, sizeof(sess->write_msg), "%s", "reader error");
                }
#endif
                tagops::Card none;
                memset(&none, 0, sizeof(none));
                none.kind = tagops::NONE;
                state.job_card = none;
                state.result = (tagops::Result){false, Reader::ERROR, 0xFF, "reader error"};
                state.present = false;
                state.phase = DONE;
                touch_state();
                xSemaphoreGive(lock);
            }
            have_cached = false;
            continue;
        }
        if (!state.reader_ok) {
            polled = true;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        bool poll_failed = false;
        try {
            poll_presence(cached, have_cached, misses);
        } catch (...) {
            poll_failed = true;
        }
        if (poll_failed) {
            reader.antenna(false);
            have_cached = false;
        }
        polled = true;
        vTaskDelay(pdMS_TO_TICKS(350));
    }
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}

void init(const Reader::Pins &pins, const uint8_t master_key[bambu::MASTER_KEY_SIZE]) {
    lock = xSemaphoreCreateMutex();
    life = xSemaphoreCreateMutex();
    memset(&state, 0, sizeof(state));
    reader_pins = pins;
    memcpy(boot_key, master_key, sizeof(boot_key));
    tagops::begin(reader);
    xSemaphoreTake(lock, portMAX_DELAY);
    apply_key(master_key);
    xSemaphoreGive(lock);
}

bool acquire() {
    bool started = false;
    xSemaphoreTake(life, portMAX_DELAY);
    holders++;
    if (task_alive) {
        // Already running (or lingering): keep it. A lingering task must be
        // woken for a fresh poll, so the caller can wait for it.
        stop_requested = false;
        no_linger = false;
        if (lingering) {
            polled = false;
            started = true;
        }
    } else {
        // Not running: start it. This also retries after an earlier start that
        // failed (e.g. low heap) while a holder is still here, not only on the
        // 0->1 transition. reader.begin()/build_codecs()/new can throw (SPI
        // init, bad_alloc); a throw must not escape with `life` still held, or
        // every later acquire/release would deadlock.
        try {
            // Right after net_link_pause() the websocket client's task has
            // ended, but its stack goes back to the heap a moment later (the
            // idle task frees it): wait up to 200 ms for a block the reader
            // task fits in, rather than fail the session.
            for (int i = 0; i < 10 && heap_caps_get_largest_free_block(
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < TASK_STACK + 512;
                 i++)
                vTaskDelay(pdMS_TO_TICKS(20));
            if (!reader_begun) {
                // The SPI bus and the chip come up once and stay up; only the
                // field and the task come and go.
                uint8_t version = reader.begin(reader_pins);
                reader_begun = true;
                xSemaphoreTake(lock, portMAX_DELAY);
                state.version = version;
                state.reader_ok = version != 0x00 && version != 0xFF;
                touch_state();
                xSemaphoreGive(lock);
                if (!state.reader_ok)
                    ESP_LOGE(TAG, TW_READER_NAME " not answering (version 0x%02X): check wiring",
                             version);
            }
            reader.antenna(false);
#if defined(TW_READER_UNIVERSAL)
            if (!codecs_built)
                build_codecs(boot_key);
#endif
            polled = false;
            stop_requested = false;
            Session *fresh = new (std::nothrow) Session();
            if (!fresh) {
                ESP_LOGE(TAG, "no memory for a reader session");
            } else {
                xSemaphoreTake(lock, portMAX_DELAY);
                sess = fresh;
                xSemaphoreGive(lock);
                task_alive = true;
                BaseType_t made = pdFAIL;
                for (int i = 0; made != pdPASS && i < 5; i++) {
                    if (i)
                        vTaskDelay(pdMS_TO_TICKS(20));  // as above: a stack still being freed
                    made = xTaskCreatePinnedToCore(task, "rfid", TASK_STACK, nullptr, 4,
                                                   &task_handle, 0);
                }
                if (made != pdPASS) {
                    ESP_LOGE(TAG, "no memory for the reader task");
                    task_alive = false;
                    xSemaphoreTake(lock, portMAX_DELAY);
                    delete sess;
                    sess = nullptr;
                    xSemaphoreGive(lock);
                } else {
                    started = true;
                }
            }
        } catch (const std::exception &e) {
            ESP_LOGE(TAG, "reader start failed: %s", e.what());
            task_alive = false;
            xSemaphoreTake(lock, portMAX_DELAY);
            delete sess;
            sess = nullptr;
            xSemaphoreGive(lock);
        } catch (...) {
            ESP_LOGE(TAG, "reader start failed");
            task_alive = false;
        }
    }
    xSemaphoreGive(life);
    return started;
}

void release(bool linger) {
    xSemaphoreTake(life, portMAX_DELAY);
    if (holders > 0 && --holders == 0 && task_alive) {
        stop_requested = true;
        no_linger = !linger;
        xSemaphoreTake(lock, portMAX_DELAY);
        cancel_requested = true;  // a job still waiting for a tag gives up
        xSemaphoreGive(lock);
    }
    xSemaphoreGive(life);
}

unsigned stack_left() {
    xSemaphoreTake(life, portMAX_DELAY);
    unsigned left = task_alive && task_handle ? (unsigned)uxTaskGetStackHighWaterMark(task_handle) : 0;
    xSemaphoreGive(life);
    return left;
}

bool ready() {
    xSemaphoreTake(life, portMAX_DELAY);
    bool up = task_alive && !stop_requested;
    xSemaphoreGive(life);
    return up;
}

bool running() {
    if (!life)
        return false;  // init() has not run
    xSemaphoreTake(life, portMAX_DELAY);
    bool up = task_alive;
    xSemaphoreGive(life);
    return up;
}

void wait_first_poll(uint32_t timeout_ms) {
    int64_t until = now_ms() + timeout_ms;
    while (!polled && now_ms() < until)
        vTaskDelay(pdMS_TO_TICKS(20));
}

}  // namespace tagsvc

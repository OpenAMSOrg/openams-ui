/**
 * @file strings.c
 * See strings.h.
 */

#include "view/strings.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "view/strings_keys.gen.h"
#include "view/strings_table.gen.h"

static int s_truncations;

/* -------------------------------------------------------------- language */

/* The language every string is looked up in. The tables are const and built in
 * (flash), so this is the only mutable state the strings own. English is index
 * 0 by construction (tools/gen_strings.mjs orders it first) and the fallback
 * for every id a language leaves out. The table list, k_tables, is generated
 * with the tables, so a new language cannot be left out of it. */
static int s_lang;

const char *view_str(view_str_id_t id)
{
    const char *text;

    if(id < 0 || id >= VS_COUNT) return "";
    text = k_tables[s_lang][id];
    if(!text) text = k_table_en[id];     /* the language has no text for this id */
    return text ? text : "";
}

const char *view_setting_key(int i)
{
    if(i < 0 || i >= VIEW_SETTING_KEY_COUNT) return "";
    return k_setting_key[i];
}

int view_lang_count(void)
{
    return VS_LANG_COUNT;
}

const char *view_lang_code(int i)
{
    return (i < 0 || i >= VS_LANG_COUNT) ? "" : k_lang_code[i];
}

const char *view_lang_name(int i)
{
    return (i < 0 || i >= VS_LANG_COUNT) ? "" : k_lang_name[i];
}

int view_lang_current(void)
{
    return s_lang;
}

bool view_set_language(const char *code)
{
    int i;

    if(!code) return false;
    for(i = 0; i < VS_LANG_COUNT; i++) {
        if(strcmp(k_lang_code[i], code) == 0) {
            s_lang = i;
            return true;
        }
    }
    return false;
}

int view_str_truncations(void)
{
    return s_truncations;
}

void view_str_truncations_reset(void)
{
    s_truncations = 0;
}

/* ---------------------------------------------------------------- writer */

/* Everything below composes text into a caller's fixed buffer through this
 * writer, so one place decides how a too-long string is cut: never inside a
 * UTF-8 character, which a CJK glyph or an accented letter would be. A CJK
 * character is three bytes, so a byte-wise cut (what snprintf() does) would
 * leave half a character behind; the view would draw a replacement glyph. */

typedef struct {
    char *out;
    size_t n;         /* the buffer's size, the NUL included */
    size_t o;         /* bytes written so far */
} vs_writer_t;

/** The bytes of the UTF-8 character that starts with @p b (1 for ASCII), 0 for
 *  a byte that cannot start one. */
static size_t utf8_len(unsigned char b)
{
    if(b < 0x80) return 1;
    if((b & 0xE0) == 0xC0) return 2;
    if((b & 0xF0) == 0xE0) return 3;
    if((b & 0xF8) == 0xF0) return 4;
    return 0;
}

/** Append the @p len bytes at @p s whole characters while they fit; count a
 *  cut if one did not. A span that ends inside a character takes the bytes it
 *  has: the table's own segments never do, but a placeholder's text is
 *  whatever a caller passed, so the writer does not assume. */
static void vs_write_span(vs_writer_t *w, const char *s, size_t len)
{
    const char *end;

    if(!s || w->n == 0) return;
    end = s + len;
    while(s < end) {
        size_t clen = utf8_len((unsigned char)*s);

        if(clen == 0) clen = 1;                       /* not UTF-8: a byte is a byte */
        if(s + clen > end) clen = (size_t)(end - s);
        if(w->o + clen + 1 > w->n) break;              /* would not fit whole */
        memcpy(w->out + w->o, s, clen);
        w->o += clen;
        s += clen;
    }
    if(s < end) s_truncations++;
    w->out[w->o] = '\0';
}

/** vs_write_span() over the whole of a NUL-terminated string. An empty or
 *  out-of-range @p w leaves the buffer as it was, so an out-of-range id still
 *  gives "" rather than nothing at all. */
static void vs_write(vs_writer_t *w, const char *s)
{
    if(!s) return;
    vs_write_span(w, s, strlen(s));
}

/** Append a printf-formatted piece (a number, a fixed unit). The piece is
 *  formatted into a scratch buffer first, so a %s in @p fmt can never grow
 *  without bound on the caller's stack. */
static void vs_printf(vs_writer_t *w, const char *fmt, ...)
{
    char tmp[48];
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if(rc < 0) return;
    if((size_t)rc >= sizeof tmp) {                     /* an absurd number: cut it */
        tmp[sizeof tmp - 1] = '\0';
        s_truncations++;
    }
    vs_write(w, tmp);
}

static void vs_begin(vs_writer_t *w, char *out, size_t n)
{
    w->out = out;
    w->n = n;
    w->o = 0;
    if(n) out[0] = '\0';
}

/* ------------------------------------------------------------- templates */

/* One named argument of a whole-sentence template (docs/design/UNIFIED_UI.md
 * 12): the type and the VS_ARG_* constructors are in strings.h, next to
 * vs_format()'s own contract. Nothing below builds a sentence out of English
 * fragments any more - the vs_* functions after this one only hand the
 * template its parts. */

/** The argument called @p name, or NULL. An unknown placeholder is not an
 *  error: it is left standing, so a caller that forgot an argument shows
 *  "{name}" and nothing is silently dropped. */
static const vs_arg_t *vs_arg(const vs_arg_t *args, size_t nargs, const char *name, size_t len)
{
    size_t i;

    for(i = 0; i < nargs; i++) {
        if(args[i].name && strncmp(args[i].name, name, len) == 0 && args[i].name[len] == '\0')
            return &args[i];
    }
    return NULL;
}

/* ------------------------------------------------------------ case forms */

/* A language whose nouns change with their role asks for a placeholder in a
 * case ("Сбросить {spool@acc}?") and lists the case forms of the placeholder's
 * own text ("SPOOL_LABEL@acc": "Катушку {n}", strings_table.gen.h). The text
 * arrives finished ("Катушка 3"), so it is matched against the form's base
 * template ("Катушка {n}") and rebuilt from the parts the match recovers. Text
 * no base matches (a spool's own name) goes in as it is. The same functions
 * in web/logic/strings.ts are the other half of the pair. */

#define VS_MAX_CAPS 4

/* One placeholder's part of the text, recovered by vs_match(). */
typedef struct {
    const char *name;
    size_t name_len;
    const char *val;
    size_t val_len;
} vs_cap_t;

/* The first place @p s holds the @p n bytes at @p lit, or NULL. */
static const char *vs_find(const char *s, const char *lit, size_t n)
{
    for(; *s; s++)
        if(strncmp(s, lit, n) == 0) return s;
    return NULL;
}

/* Whether @p text is template @p tpl filled in: its literal parts must be there
 * exactly, and each placeholder takes the text up to the next literal part (to
 * the end, when it is the last thing). Fills @p caps on a match. */
static bool vs_match(const char *tpl, const char *text, vs_cap_t *caps, int *ncaps)
{
    const char *t = tpl;
    const char *x = text;

    *ncaps = 0;
    while(*t) {
        const char *close, *lit, *lit_end, *at;
        size_t lit_len;

        if(*t != '{') {
            if(*x != *t) return false;
            t++;
            x++;
            continue;
        }
        close = strchr(t, '}');
        if(!close || *ncaps >= VS_MAX_CAPS) return false;
        lit = close + 1;
        lit_end = strchr(lit, '{');
        lit_len = lit_end ? (size_t)(lit_end - lit) : strlen(lit);
        if(lit_len == 0) {
            if(lit_end) return false;          /* two placeholders back to back */
            at = x + strlen(x);
        }
        else {
            at = vs_find(x, lit, lit_len);
            if(!at) return false;
        }
        if(at == x) return false;              /* a placeholder is never empty */
        caps[*ncaps].name = t + 1;
        caps[*ncaps].name_len = (size_t)(close - t - 1);
        caps[*ncaps].val = x;
        caps[*ncaps].val_len = (size_t)(at - x);
        (*ncaps)++;
        x = at;
        t = close + 1;
    }
    return *x == '\0';
}

/* Write case form @p tpl, its placeholders taken from @p caps. */
static void vs_write_form(vs_writer_t *w, const char *tpl, const vs_cap_t *caps, int ncaps)
{
    const char *t = tpl;
    const char *lit = tpl;

    while(*t) {
        const char *close;
        int i;

        if(*t != '{') { t++; continue; }
        close = strchr(t, '}');
        if(!close) break;
        vs_write_span(w, lit, (size_t)(t - lit));
        for(i = 0; i < ncaps; i++)
            if(caps[i].name_len == (size_t)(close - t - 1)
               && strncmp(caps[i].name, t + 1, caps[i].name_len) == 0) break;
        if(i < ncaps) vs_write_span(w, caps[i].val, caps[i].val_len);
        else vs_write_span(w, t, (size_t)(close + 1 - t));
        t = close + 1;
        lit = t;
    }
    vs_write_span(w, lit, strlen(lit));
}

/* @p text in case @p kase: rebuilt from the current language's first form in
 * that case whose base @p text matches; @p text as it is when none does. */
static void vs_write_case(vs_writer_t *w, const char *text, const char *kase, size_t kase_len)
{
    const vs_form_t *f = k_forms[s_lang];
    vs_cap_t caps[VS_MAX_CAPS];
    int ncaps;

    for(; f && f->id >= 0; f++) {
        if(strlen(f->kase) != kase_len || strncmp(f->kase, kase, kase_len) != 0) continue;
        if(vs_match(view_str((view_str_id_t)f->id), text, caps, &ncaps)) {
            vs_write_form(w, f->text, caps, ncaps);
            return;
        }
    }
    vs_write(w, text);
}

void vs_format(view_str_id_t id, char *out, size_t n, const vs_arg_t *args, size_t nargs)
{
    vs_writer_t w;
    const char *t = view_str(id);
    const char *lit = t;

    vs_begin(&w, out, n);
    while(*t) {
        const char *open, *close;
        const vs_arg_t *a;

        if(*t != '{') { t++; continue; }
        vs_write_span(&w, lit, (size_t)(t - lit));   /* the literal before the '{' */
        open = t + 1;
        close = strchr(open, '}');
        if(!close) {                                 /* a '{' with no '}': literal */
            vs_write_span(&w, lit, strlen(lit));
            return;
        }
        {
            /* "{name@case}": the argument `name`, in that case (see above). */
            const char *at = memchr(open, '@', (size_t)(close - open));

            a = vs_arg(args, nargs, open, at ? (size_t)(at - open) : (size_t)(close - open));
            if(!a) {
                vs_write_span(&w, t, (size_t)(close + 1 - t));   /* leave "{name}" standing */
            }
            else if(a->text && at) {
                vs_write_case(&w, a->text, at + 1, (size_t)(close - at - 1));
            }
            else if(a->text) {
                vs_write(&w, a->text);
            }
            else if(a->decimals == 2) {
                vs_printf(&w, "%.2f", (double)a->value);
            }
            else if(a->decimals == 3) {
                vs_printf(&w, "%.3f", (double)a->value);
            }
            else {
                vs_printf(&w, "%d", a->value);
            }
        }
        t = close + 1;
        lit = t;
    }
    vs_write_span(&w, lit, strlen(lit));
}

/* ---------------------------------------------------------------- formats */

/* Each of the functions below hands one template its parts and nothing else:
 * the sentence, its punctuation and the order of its words are the string
 * table's, not this file's. The same-named functions in web/logic/strings.ts
 * are the other half of the pair, and the differential tests hold them
 * identical. */

void vs_pct(int pct, char *out, size_t n)
{
    vs_arg_t a[1];

    if(pct < 0) { vs_copy(view_str(VS_UNKNOWN_MARK), out, n); return; }
    a[0] = (vs_arg_t)VS_ARG_N("pct", pct);
    vs_format(VS_PCT, out, n, a, 1);
}

void vs_grams(int grams, char *out, size_t n)
{
    vs_arg_t a[1];

    if(grams < 0) { if(n) out[0] = '\0'; return; }
    a[0] = (vs_arg_t)VS_ARG_N("g", grams);
    vs_format(VS_GRAMS, out, n, a, 1);
}

/* A reading and a tick label are numbers, not sentences: there is no word to
 * translate and no word order to change, so they are printed here rather than
 * given a template id. A locale's decimal separator is a number-format concern
 * for i18n-b, not a string table one. */
void vs_pressure(float value, char *out, size_t n)
{
    vs_writer_t w;

    vs_begin(&w, out, n);
    if(value < 0.0f) return;
    vs_printf(&w, "%.3f", (double)value);
}

void vs_scale(float value, char *out, size_t n)
{
    vs_writer_t w;

    vs_begin(&w, out, n);
    vs_printf(&w, "%.2f", (double)value);
}

void vs_env(bool has_pct, int pct, bool has_temp, int temp_c, char *out, size_t n)
{
    vs_arg_t a[2];

    if(has_pct && has_temp) {
        a[0] = (vs_arg_t)VS_ARG_N("rh", pct);
        a[1] = (vs_arg_t)VS_ARG_N("temp", temp_c);
        vs_format(VS_ENV_BOTH, out, n, a, 2);
    }
    else if(has_pct) {
        a[0] = (vs_arg_t)VS_ARG_N("rh", pct);
        vs_format(VS_ENV_PCT, out, n, a, 1);
    }
    else if(has_temp) {
        a[0] = (vs_arg_t)VS_ARG_N("temp", temp_c);
        vs_format(VS_ENV_TEMP, out, n, a, 1);
    }
    else if(n) {
        out[0] = '\0';
    }
}

void vs_shortfall(int tool_index, int need_g, int have_g, char *out, size_t n)
{
    vs_arg_t a[3];

    a[0] = (vs_arg_t)VS_ARG_N("tool", tool_index);
    a[1] = (vs_arg_t)VS_ARG_N("need", need_g);
    a[2] = (vs_arg_t)VS_ARG_N("have", have_g);
    vs_format(VS_SHORTFALL, out, n, a, 3);
}

void vs_spare(int position, char *out, size_t n)
{
    vs_arg_t a[2];

    if(position < 0) { vs_copy(view_str(VS_INFINITY_MARK), out, n); return; }
    a[0] = (vs_arg_t)VS_ARG_T("mark", view_str(VS_INFINITY_MARK));
    a[1] = (vs_arg_t)VS_ARG_N("n", position);
    vs_format(VS_SPARE_NUMBERED, out, n, a, 2);
}

void vs_copy(const char *s, char *out, size_t n)
{
    vs_writer_t w;

    vs_begin(&w, out, n);
    vs_write(&w, s);
}

void vs_dryer_active(int target_c, int remaining_min, char *out, size_t n)
{
    vs_arg_t a[2];

    a[0] = (vs_arg_t)VS_ARG_N("temp", target_c);
    a[1] = (vs_arg_t)VS_ARG_N("hours", (remaining_min > 0) ? (remaining_min + 59) / 60 : 0);
    vs_format(VS_DRYER_ACTIVE, out, n, a, 2);
}

void vs_spool_label(int bay, char *out, size_t n)
{
    vs_arg_t a[1];

    a[0] = (vs_arg_t)VS_ARG_N("n", bay + 1);
    vs_format(VS_SPOOL_LABEL, out, n, a, 1);
}

/* An extruder's display name is a name, not a sentence: underscores removed
 * and each word capitalized, with no wording to translate. */
void vs_extruder_title(const char *name, char *out, size_t n)
{
    vs_writer_t w;
    bool word_start = true;
    char c;

    vs_begin(&w, out, n);
    for(; name && *name; name++) {
        c = *name;
        if(c == '_') { word_start = true; continue; }
        if(word_start && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        word_start = false;
        if(w.o + 2 > w.n) { s_truncations++; break; }
        w.out[w.o++] = c;
        w.out[w.o] = '\0';
    }
}

void vs_runout(const char *tool, const char *spool, char *out, size_t n)
{
    vs_arg_t a[2];

    a[0] = (vs_arg_t)VS_ARG_T("tool", tool);
    if(!spool || !spool[0]) {
        vs_format(VS_RUNOUT_ALONE, out, n, a, 1);
        return;
    }
    a[1] = (vs_arg_t)VS_ARG_T("spool", spool);
    vs_format(VS_RUNOUT, out, n, a, 2);
}

void vs_runout_no_spare(const char *tool, char *out, size_t n)
{
    vs_arg_t a[1];

    a[0] = (vs_arg_t)VS_ARG_T("tool", tool);
    vs_format(VS_RUNOUT_NO_SPARE, out, n, a, 1);
}

void vs_unit_spool(const char *unit, int bay, char *out, size_t n)
{
    vs_arg_t a[2];

    a[0] = (vs_arg_t)VS_ARG_T("unit", unit);
    a[1] = (vs_arg_t)VS_ARG_N("n", bay + 1);
    vs_format(VS_UNIT_SPOOL, out, n, a, 2);
}

void vs_unit_bay_label(const char *unit, int bay, char *out, size_t n)
{
    vs_arg_t a[2];

    a[0] = (vs_arg_t)VS_ARG_T("unit", unit);
    a[1] = (vs_arg_t)VS_ARG_N("n", bay + 1);
    vs_format(VS_UNIT_BAY, out, n, a, 2);
}

void vs_tool_spool(const char *tool, const char *unit, int bay, char *out, size_t n)
{
    vs_arg_t a[3];

    a[0] = (vs_arg_t)VS_ARG_T("tool", tool);
    a[1] = (vs_arg_t)VS_ARG_T("unit", unit);
    a[2] = (vs_arg_t)VS_ARG_N("n", bay + 1);
    vs_format(VS_TOOL_SPOOL, out, n, a, 3);
}

/* A picker label: which parts are there decides the id, so the word order and
 * the separator around the weight belong to the language. */
void vs_spool_option(const char *vendor, const char *material, int grams, char *out, size_t n)
{
    vs_arg_t a[3];
    bool has_v = vendor && vendor[0], has_m = material && material[0], has_g = grams >= 0;
    view_str_id_t id;

    if(has_v && has_m)  id = has_g ? VS_SPOOL_OPTION_FULL : VS_SPOOL_OPTION_NO_GRAMS;
    else if(has_v)       id = has_g ? VS_SPOOL_OPTION_VENDOR_GRAMS : VS_SPOOL_OPTION_VENDOR;
    else if(has_m)       id = has_g ? VS_SPOOL_OPTION_MATERIAL_GRAMS : VS_SPOOL_OPTION_MATERIAL;
    else                id = VS_SPOOL_OPTION_GRAMS;

    a[0] = (vs_arg_t)VS_ARG_T("vendor", has_v ? vendor : "");
    a[1] = (vs_arg_t)VS_ARG_T("material", has_m ? material : "");
    a[2] = (vs_arg_t)VS_ARG_N("g", grams);
    vs_format(id, out, n, a, 3);
}

/* "<spool> reported an error": the message row's line for a bay in error that
 * no lane fault covers. */
void vs_spool_error(const char *spool, char *out, size_t n)
{
    vs_arg_t a[1];

    a[0] = (vs_arg_t)VS_ARG_T("spool", spool);
    vs_format(VS_MESSAGE_SPOOL_ERROR, out, n, a, 1);
}

/* "Low filament: <spool> has <g> g left": the alert the status mapper raises
 * when a bay first drops below 15 % (src/backend/status_map.c). */
void vs_low_filament(const char *spool, int grams, char *out, size_t n)
{
    vs_arg_t a[2];

    a[0] = (vs_arg_t)VS_ARG_T("spool", spool);
    a[1] = (vs_arg_t)VS_ARG_N("g", grams);
    vs_format(VS_ALERT_LOW_FILAMENT, out, n, a, 2);
}

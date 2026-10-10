/**
 * @file view.c
 * See view.h.
 *
 * Every builder reads only the model it is given: the group, slot-id and
 * spare helpers it reaches (mmu_slot_group_in(), mmu_group_next_spare_in(),
 * mmu_slot_id_in()) and the action dry-run (actions_check(), actions_map.c)
 * all take that model pointer, never the g_mmu singleton, so a second model
 * (the wasm target's, a test's) is built from itself alone.
 */

#include "view/view.h"

#include "backend/actions_map.h"
#include "view/strings.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------- geometry */

/* Relative luminance, plain 0..255 channels (docs/design/UNIFIED_UI.md 5 /
 * design/tokens.json "ink_threshold"): 0.2126 R + 0.7152 G + 0.0722 B, scaled
 * x10000 to stay integer. > 160 (i.e. > 1 600 000 scaled) reads as bright
 * enough for dark text. */
static view_ink_t ink_for(uint32_t rgb)
{
    long r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    long lum = 2126L * r + 7152L * g + 722L * b;

    return (lum > 1600000L) ? VIEW_INK_DARK : VIEW_INK_LIGHT;
}

/* Whether a filament color is too close to the dark surface to read, so the
 * renderer draws a light 1 px outline around it (UNIFIED_UI 4g). Decided here,
 * once, for both cores: the relative luminance with a gamma of 2 (integer, so
 * the two cores cannot disagree), scaled x10000 x 255^2. Below 0.065 is a
 * contrast under about 1.5:1 against the lightest surface (UI_TOK_SURFACE_1,
 * #22262C, WCAG luminance 0.019): black (0) and anything up to about #414141
 * are outlined, a mid gray (#808080) and every saturated mid color are not. */
#define OUTLINE_LUM_MAX 42266250L
static bool outline_for(uint32_t rgb)
{
    long r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

    return 2126L * r * r + 7152L * g * g + 722L * b * b < OUTLINE_LUM_MAX;
}

static uint8_t mix_channel(uint32_t fg, uint32_t bg, int pct_fg)
{
    long v = (fg * pct_fg + bg * (100 - pct_fg) + 50) / 100;

    if(v < 0) v = 0;
    if(v > 255) v = 255;
    return (uint8_t)v;
}

static uint32_t mix_rgb(uint32_t fg, uint32_t bg, int pct_fg)
{
    uint32_t r = mix_channel((fg >> 16) & 0xFF, (bg >> 16) & 0xFF, pct_fg);
    uint32_t g = mix_channel((fg >> 8) & 0xFF, (bg >> 8) & 0xFF, pct_fg);
    uint32_t b = mix_channel(fg & 0xFF, bg & 0xFF, pct_fg);

    return (r << 16) | (g << 8) | b;
}

/* design/tokens.json "tile": dark_ink (a bright tile, dark text) mixes MORE
 * of the filament in (78%/42%); light_ink (a dark tile, light text) mixes in
 * even more still (96%/74%) since its base is pure black rather than #141414. */
static void gradient_for(uint32_t rgb, view_ink_t ink, uint32_t *top, uint32_t *bottom)
{
    if(ink == VIEW_INK_DARK) {
        *top = mix_rgb(rgb, 0x141414u, 78);
        *bottom = mix_rgb(rgb, 0x1a1a1au, 42);
    }
    else {
        *top = mix_rgb(rgb, 0x000000u, 96);
        *bottom = mix_rgb(rgb, 0x1a1a1au, 74);
    }
}

/* -------------------------------------------------------------- groups */

/* docs/design/UNIFIED_UI.md 4, "Spare order": position counts the group's
 * non-loaded members in fallback (declared) order; the loaded tile itself
 * gets position null (drawn bare), only when the group has a usable spare
 * somewhere - "with no spare there is no badge". */
static void compute_spare(const mmu_model_t *m, int gi, int unit, int slot, view_spare_t *out)
{
    const mmu_group_t *g = &m->groups[gi];
    bool is_loaded = (m->units[unit].slots[slot].state == MMU_SLOT_LOADED);
    int mi, rank = 0, non_loaded = 0;

    memset(out, 0, sizeof(*out));

    /* "A group with one member has no spare" (mmu_group_next_spare's own
     * rule, docs/GROUPS.md): nothing to fall back to, so no badge on its one
     * member either way. */
    if(g->count <= 1) return;

    for(mi = 0; mi < (int)g->count; mi++) {
        int u = g->members[mi].unit, s = g->members[mi].slot;

        if(u < 0 || u >= (int)m->unit_count) continue;
        if(s < 0 || s >= (int)m->units[u].slot_count) continue;
        if(m->units[u].slots[s].state == MMU_SLOT_LOADED) continue;
        non_loaded++;
    }
    out->count = non_loaded;

    if(is_loaded) {
        {
            int su, ss;

            out->present = mmu_group_next_spare_in(m, gi, &su, &ss);
        }
        out->bare = true;
        out->position = -1;
        return;
    }

    for(mi = 0; mi < (int)g->count; mi++) {
        int u = g->members[mi].unit, s = g->members[mi].slot;

        if(u < 0 || u >= (int)m->unit_count) continue;
        if(s < 0 || s >= (int)m->units[u].slot_count) continue;
        if(m->units[u].slots[s].state == MMU_SLOT_LOADED) continue;
        rank++;
        if(u == unit && s == slot) {
            out->present = true;
            out->bare = false;
            out->position = rank;
            return;
        }
    }
}

/* The slicer tool index a group named "T<n>" feeds (mmu_model_tool_shortfall
 * keys job_need_g[] the same way); -1 when @p name is not that shape. */
static int group_tool_index(const char *name)
{
    int n;

    if(!name || name[0] != 'T') return -1;
    if(sscanf(name + 1, "%d", &n) != 1) return -1;
    if(n < 0 || n >= MMU_MAX_TOOLS) return -1;
    return n;
}

/* -------------------------------------------------------------- actions */

/* The next free slot of @p o, cleared and filled with the parts every action
 * has; NULL when @p o is full (the caller just stops adding). */
static view_action_t *act_add(view_actions_t *o, const char *id, view_str_id_t label,
                              const char *line, view_action_style_t style)
{
    view_action_t *a;

    if(o->count >= VIEW_ACTIONS_MAX) return NULL;
    a = &o->items[o->count++];
    memset(a, 0, sizeof(*a));
    vs_copy(id, a->id, sizeof(a->id));
    a->label = view_str(label);
    vs_copy(line, a->line, sizeof(a->line));
    a->style = style;
    return a;
}

static void action_clear(view_action_t *a)
{
    memset(a, 0, sizeof(*a));
}

static void action_confirm(view_action_t *a, view_str_id_t title, view_str_id_t text,
                           view_str_id_t ok)
{
    a->confirm.present = true;
    a->confirm.title = view_str(title);
    a->confirm.text = view_str(text);
    a->confirm.ok_label = view_str(ok);
}

/* Dry-run @p line (already resolved - no {placeholder} left in it) and point
 * @p a's reason at the one predicate's own words: a fixed string from the
 * table, or the host's line when the command could not even be built. Never a
 * copy, so a translation of any length survives. */
static void action_check(view_action_t *a, const char *line, const mmu_model_t *m)
{
    const char *why = "";

    a->enabled = actions_check(line, m, &why) ? true : false;
    a->reason = a->enabled ? "" : why;
}

/* A plain action: the line is built once, sent as it stands and dry-run as it
 * stands. */
static view_action_t *act_simple(view_actions_t *o, const mmu_model_t *m, const char *id,
                                 view_str_id_t label, view_action_style_t style,
                                 const char *fmt, const char *arg)
{
    char line[VIEW_ACTION_LINE_LEN];
    view_action_t *a;

    snprintf(line, sizeof line, fmt, arg);
    a = act_add(o, id, label, line, style);
    if(a) action_check(a, line, m);
    return a;
}

/* The next free field of @p o's pool for @p a's form, cleared; NULL when the
 * pool is full. The form's fields must be added one after another. */
static view_field_t *form_field(view_actions_t *o, view_action_t *a)
{
    view_field_t *f;

    if(o->field_count >= VIEW_MAX_FIELDS || a->form.field_count >= VIEW_MAX_FORM_FIELDS) return NULL;
    if(!a->form.present) {
        a->form.present = true;
        a->form.field_first = o->field_count;
    }
    f = &o->fields[o->field_count++];
    a->form.field_count++;
    memset(f, 0, sizeof(*f));
    f->unit = "";              /* a fixed string, "" rather than NULL */
    return f;
}

static void fault_actions(const mmu_model_t *m, const mmu_toolhead_t *th, view_actions_t *out);

/* ------------------------------------------------------------------ colors */

/* The colors the spool editor offers by name: the same 18 the display's
 * palette sheet draws (the nearest entry names any other color, so a tag
 * read or an off-shade still gets a sensible name). */
static const struct {
    uint32_t rgb;
    view_str_id_t name;
} k_palette[] = {
    { 0xFFFFFF, VS_COLOR_WHITE },
    { 0xA6A9AA, VS_COLOR_SILVER },
    { 0x5F6368, VS_COLOR_GRAY },
    { 0x000000, VS_COLOR_BLACK },
    { 0x9D432C, VS_COLOR_BROWN },
    { 0xE4BD68, VS_COLOR_GOLD },
    { 0xC12E1F, VS_COLOR_RED },
    { 0x9D2235, VS_COLOR_MAROON },
    { 0xFF6A13, VS_COLOR_ORANGE },
    { 0xF4EE2A, VS_COLOR_YELLOW },
    { 0x00AE42, VS_COLOR_GREEN },
    { 0x1F5C3A, VS_COLOR_FOREST_GREEN },
    { 0x00B1B7, VS_COLOR_TURQUOISE },
    { 0x0086D6, VS_COLOR_CYAN },
    { 0x0A2989, VS_COLOR_BLUE },
    { 0x5E43B7, VS_COLOR_PURPLE },
    { 0xEC008C, VS_COLOR_MAGENTA },
    { 0xF55A74, VS_COLOR_PINK },
};
#define PALETTE_COUNT ((int)(sizeof k_palette / sizeof k_palette[0]))

int view_palette_count(void)
{
    return PALETTE_COUNT;
}

uint32_t view_palette_color(int i)
{
    return (i >= 0 && i < PALETTE_COUNT) ? k_palette[i].rgb : 0;
}

const char *view_palette_name(int i)
{
    return (i >= 0 && i < PALETTE_COUNT) ? view_str(k_palette[i].name) : "";
}

const char *view_color_name(uint32_t rgb)
{
    int best = 0, i;
    long best_d = -1;
    int r = (int)((rgb >> 16) & 0xFF), g = (int)((rgb >> 8) & 0xFF), b = (int)(rgb & 0xFF);

    for(i = 0; i < PALETTE_COUNT; i++) {
        int cr = (int)((k_palette[i].rgb >> 16) & 0xFF);
        int cg = (int)((k_palette[i].rgb >> 8) & 0xFF);
        int cb = (int)(k_palette[i].rgb & 0xFF);
        long d = (long)(r - cr) * (r - cr) + (long)(g - cg) * (g - cg) + (long)(b - cb) * (b - cb);

        if(best_d < 0 || d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return view_palette_name(best);
}

/* -------------------------------------------------------------------- tile */

static void build_tile_content(const mmu_model_t *m, int unit, int bay,
                               const mmu_unit_t *u, const mmu_slot_t *s, view_tile_t *out)
{
    int gi = mmu_slot_group_in(m, unit, bay);
    const mmu_toolhead_t *th = NULL;
    bool empty, unknown;

    out->tag = "";        /* a fixed string: "" rather than NULL, tag or no tag */
    if(u->toolhead >= 0 && u->toolhead < (int)m->toolhead_count)
        th = &m->toolheads[u->toolhead];

    /* The label is the tool, or "?" for a bay in no group; the sublabel is
     * always the bay's "Spool N" (UNIFIED_UI 4, 4a). */
    if(gi >= 0) {
        out->has_tool = true;
        vs_copy(m->groups[gi].name, out->tool, sizeof(out->tool));
        vs_copy(out->tool, out->label, sizeof(out->label));
        compute_spare(m, gi, unit, bay, &out->spare);
    }
    else {
        vs_copy(view_str(VS_UNKNOWN_MARK), out->label, sizeof(out->label));
    }
    out->has_sublabel = true;
    vs_spool_label(bay, out->sublabel, sizeof(out->sublabel));

    /* state, in the order a fault or a motion in progress outranks a plain
     * material reading: error > runout > loading/unloading > positioning >
     * the raw slot state (empty "Not inserted", unknown/hatched, loaded,
     * ready). */
    if(s->state == MMU_SLOT_ERROR) {
        out->state = VIEW_TILE_ERROR;
        out->has_tag = true;
        out->tag_tone = VIEW_TONE_ERROR;
        out->tag = view_str(VS_TAG_ERROR);
    }
    else if(th && th->runout_active && th->runout_from_unit == unit && th->runout_from_slot == bay) {
        out->state = VIEW_TILE_RUNOUT;
        out->has_tag = true;
        out->tag_tone = VIEW_TONE_INFO;
        out->tag = view_str(VS_TAG_RUNOUT);
    }
    else if(th && th->busy != MMU_BUSY_NONE && th->busy_unit == unit && th->busy_slot == bay) {
        out->state = (th->busy == MMU_BUSY_UNLOAD) ? VIEW_TILE_UNLOADING : VIEW_TILE_LOADING;
        out->has_tag = true;
        out->tag_tone = VIEW_TONE_INFO;
        out->tag = view_str(th->busy == MMU_BUSY_UNLOAD ? VS_TAG_UNLOADING : VS_TAG_LOADING);
    }
    else if(s->positioning) {
        out->state = VIEW_TILE_POSITIONING;
        out->has_tag = true;
        out->tag_tone = VIEW_TONE_INFO;
        out->tag = view_str(VS_TAG_POSITIONING);
    }
    else {
        empty = (s->state == MMU_SLOT_EMPTY);
        unknown = (!empty && s->info_source == MMU_INFO_UNKNOWN);

        /* Loaded outranks "unknown": something is actively feeding from this
         * bay right now, which matters more than whether its material was
         * ever identified. A ready-but-unidentified bay, with nothing more
         * urgent going on, is "unknown". */
        if(s->state == MMU_SLOT_LOADED) {
            out->state = VIEW_TILE_LOADED;
        }
        else if(empty) {
            out->state = VIEW_TILE_EMPTY;
            out->has_tag = true;
            out->tag_tone = VIEW_TONE_NEUTRAL;
            out->tag = view_str(VS_TAG_NOT_INSERTED);
        }
        else if(unknown) {
            out->state = VIEW_TILE_UNKNOWN;
        }
        else {
            out->state = VIEW_TILE_READY;
        }
    }

    /* Nothing more urgent to say and a spool waits for the user's confirmation. */
    if(!out->has_tag && s->pending_confirmation) {
        out->has_tag = true;
        out->tag_tone = VIEW_TONE_INFO;
        out->tag = view_str(VS_TAG_CONFIRM);
    }

    if(out->has_tag) {
        view_str_id_t d = VS_TAGD_CONFIRM;

        switch(out->state) {
        case VIEW_TILE_ERROR:       d = VS_TAGD_ERROR; break;
        case VIEW_TILE_RUNOUT:      d = VS_TAGD_RUNOUT; break;
        case VIEW_TILE_LOADING:     d = VS_TAGD_LOADING; break;
        case VIEW_TILE_UNLOADING:   d = VS_TAGD_UNLOADING; break;
        case VIEW_TILE_POSITIONING: d = VS_TAGD_POSITIONING; break;
        case VIEW_TILE_EMPTY:       d = VS_TAGD_NOT_INSERTED; break;
        default:                    break;
        }
        /* A fixed string from the table, or the host's own words when the
         * fault behind this tag names it: both are borrowed, never copied. */
        out->tag_detail = view_str(d);
        if(out->state == VIEW_TILE_ERROR && th && th->has_error && th->error_unit == unit &&
           th->error_slot == bay)
            out->tag_detail = th->error_text[0] ? th->error_text : th->error_code;
        if(out->state == VIEW_TILE_ERROR && th && th->has_error && th->error_unit == unit &&
           th->error_slot == bay)
            vs_copy(th->error_code, out->tag_code, sizeof(out->tag_code));
    }

    /* One disabled look: an empty or unidentified bay, or a unit that is offline. */
    out->dim = (out->state == VIEW_TILE_EMPTY || out->state == VIEW_TILE_UNKNOWN || !u->connected);
    /* the hatched look marks "nothing to use here": not inserted, or disabled; a
     * real spool (black, unknown, error, ...) is never hatched */
    out->hatched = (out->state == VIEW_TILE_EMPTY || !u->connected);

    /* No known color: no color, no gradient, and the ink of the neutral surface
     * the renderer draws instead (PRINCIPLES 1: chrome is neutral, so never a
     * black tile that reads as black filament). A known 0x000000 is black. */
    out->color_known = s->color_known;
    if(s->color_known) {
        out->color = s->color;
        out->ink = ink_for(s->color);
        gradient_for(s->color, out->ink, &out->gradient_top, &out->gradient_bottom);
        out->color_name = view_color_name(s->color);
    }
    else {
        out->ink = VIEW_INK_LIGHT;
        out->color_name = "";
    }

    /* "name" has no field of its own in mmu_slot_t yet: until a spool-name
     * field exists, it mirrors "material". An unknown one is "": only the
     * ring shows "?" (UNIFIED_UI 4a). */
    {
        const char *mat_text = (s->state == MMU_SLOT_EMPTY) ? "" : s->material;

        vs_copy(mat_text, out->material, sizeof(out->material));
        vs_copy(mat_text, out->name, sizeof(out->name));
    }
    vs_copy(s->brand, out->brand, sizeof(out->brand));

    out->pct = s->remaining_pct;
    vs_pct(out->pct, out->pct_text, sizeof(out->pct_text));
    out->low = (out->pct >= 0 && out->pct < 15);
    vs_grams(s->remaining_g, out->grams_text, sizeof(out->grams_text));

    out->rfid = (s->info_source == MMU_INFO_TAG);
    out->pending_confirmation = s->pending_confirmation;
    out->calibrated = (s->cal_state == MMU_CAL_CONFIGURED);
}

bool view_tile(const mmu_model_t *m, int unit_idx, int bay, view_tile_t *out)
{
    const mmu_unit_t *u;
    const mmu_slot_t *s;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    u = &m->units[unit_idx];
    if(bay < 0 || bay >= (int)u->slot_count) return false;
    s = &u->slots[bay];

    mmu_slot_id_in(m, unit_idx, bay, out->slot_id, sizeof(out->slot_id));
    out->bay = bay;

    build_tile_content(m, unit_idx, bay, u, s, out);

    return true;
}

/* The select field of "assign spool": one option per spool of the model's list
 * (an id of 0 is an empty slot of that list), the bay's current link (or else
 * the first spool) as the default. */
static void add_spool_options(const mmu_model_t *m, const mmu_slot_t *s, view_actions_t *o,
                              view_field_t *f)
{
    int i;

    f->option_first = o->option_count;
    for(i = 0; i < (int)m->spool_count && o->option_count < VIEW_MAX_OPTIONS; i++) {
        const mmu_spool_t *sp = &m->spools[i];
        view_option_t *op;

        if(sp->id == 0) continue;
        op = &o->options[o->option_count++];
        op->value = sp->id;
        vs_spool_option(sp->vendor, sp->material, (int)sp->remaining_g, op->label,
                            sizeof(op->label));
        op->has_color_field = true;
        op->color_known = sp->color_known;
        op->color = sp->color_known ? (sp->color & 0xFFFFFFu) : 0;
        if(f->option_count == 0 || sp->id == s->spool_id) f->value = (double)sp->id;
        f->option_count++;
    }
}

/* One option of the pool, for the select field being built; false when the
 * pool is full. */
static bool add_option(view_actions_t *o, view_field_t *f, int32_t value, const char *label)
{
    view_option_t *op;

    if(o->option_count >= VIEW_MAX_OPTIONS) return false;
    op = &o->options[o->option_count++];
    op->value = value;
    vs_copy(label, op->label, sizeof(op->label));
    op->has_color_field = false;
    op->color_known = false;
    op->color = 0;
    f->option_count++;
    return true;
}

static view_field_t *select_field(view_actions_t *o, view_action_t *a, const char *id,
                                  view_str_id_t label)
{
    view_field_t *f = form_field(o, a);

    if(!f) return NULL;
    vs_copy(id, f->id, sizeof(f->id));
    f->label = view_str(label);
    f->kind = VIEW_FIELD_SELECT;
    f->option_first = o->option_count;
    return f;
}

static view_field_t *number_field(view_actions_t *o, view_action_t *a, const char *id,
                                  view_str_id_t label, double value, double min, double max,
                                  double step, const char *unit)
{
    view_field_t *f = form_field(o, a);

    if(!f) return NULL;
    vs_copy(id, f->id, sizeof(f->id));
    f->label = view_str(label);
    f->kind = VIEW_FIELD_NUMBER;
    f->value = value;
    f->min = min;
    f->max = max;
    f->step = step;
    f->unit = unit;
    return f;
}

/* The edit line with every field at what the form starts from: the predicate's
 * dry run of the action (it never reads as a change, see actions_check()).
 * @p choose_material starts the material on the "choose this" choice, which the
 * line spells -1 and the mapper refuses until it has been answered. */
static void edit_resolved_line(char *out, size_t n, const char *slot_id,
                               const mmu_edit_defaults_t *d, bool choose_material)
{
    snprintf(out, n, "spool edit %s material=%d color=%d vendor=%d remaining=%d initial=%d pa=%d.%03d",
             slot_id, choose_material ? -1 : d->material, (int)d->color, d->vendor,
             d->remaining_g, d->initial_g, d->pa_x1000 / 1000, d->pa_x1000 % 1000);
}

/* The spool editor's fields: material, color, vendor, both weights and the
 * pressure advance of the bay's spool, each starting from what the model knows
 * (or a default, mmu_edit_defaults). Two actions ask for them - "Edit spool",
 * and "Load" on a bay nobody has identified - so they are built once here and
 * both take them whole (PRINCIPLES.md 13: one task, one place to do it).
 *
 * @p choose_material puts the one choice only the user can make at the head of
 * the material list: the line spells it -1, and the mapper refuses that until
 * the field has been answered. */
static void add_edit_fields(view_actions_t *out, view_action_t *a, const mmu_model_t *m,
                            const mmu_slot_t *s, const mmu_edit_defaults_t *d,
                            bool choose_material)
{
    view_field_t *f;
    int i, n;
    bool in_palette = false;

    f = select_field(out, a, "material", VS_EDIT_MATERIAL);
    if(f) {
        if(choose_material) add_option(out, f, -1, view_str(VS_MATERIAL_CHOOSE));
        for(i = 0; i < MMU_MATERIAL_COUNT; i++) add_option(out, f, i, mmu_material_name(i));
        if(d->material == MMU_MATERIAL_COUNT) add_option(out, f, MMU_MATERIAL_COUNT, s->material);
        f->value = choose_material ? -1.0 : (double)d->material;
        f->required = choose_material;
    }

    f = select_field(out, a, "color", VS_EDIT_COLOR);
    if(f) {
        if(d->color < 0) add_option(out, f, -1, view_str(VS_EDIT_COLOR_UNSET));
        for(i = 0; i < PALETTE_COUNT; i++) {
            add_option(out, f, (int32_t)k_palette[i].rgb, view_str(k_palette[i].name));
            if(d->color == (int32_t)k_palette[i].rgb) in_palette = true;
        }
        if(d->color >= 0 && !in_palette) {
            char hex[16];

            snprintf(hex, sizeof hex, "#%06X", (unsigned)(d->color & 0xFFFFFF));
            add_option(out, f, d->color, hex);
        }
        f->value = (double)d->color;
    }

    f = select_field(out, a, "vendor", VS_EDIT_VENDOR);
    if(f) {
        n = mmu_edit_vendor_count(m);
        for(i = 0; i < n; i++) add_option(out, f, i, mmu_edit_vendor_name(m, i));
        if(d->vendor == n) add_option(out, f, n, s->brand);
        f->value = (double)d->vendor;
    }

    (void)number_field(out, a, "remaining", VS_FIELD_REMAINING_LABEL, (double)d->remaining_g, 0.0,
                       10000.0, 1.0, view_str(VS_FIELD_WEIGHT_UNIT));
    (void)number_field(out, a, "initial", VS_FIELD_INITIAL_LABEL, (double)d->initial_g, 1.0,
                       10000.0, 1.0, view_str(VS_FIELD_WEIGHT_UNIT));
    (void)number_field(out, a, "pa", VS_EDIT_PA, (double)d->pa_x1000 / 1000.0, 0.0, 2.0, 0.001,
                       "");
}

/* "Edit spool": the editor's fields on the bay's own spool. The one line
 * carries every field; the mapper sends only the ones the user changed
 * (actions_map.c, `spool edit`). Dimmed, with its reason, while the host cannot
 * take it: no edit endpoint, Spoolman offline or an empty bay. */
static void add_edit_action(view_actions_t *out, const mmu_model_t *m, int unit_idx, int bay,
                            const mmu_slot_t *s, const char *slot_id)
{
    mmu_edit_defaults_t d;
    char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
    view_action_t *a;

    mmu_edit_defaults(m, unit_idx, bay, &d);
    snprintf(line, sizeof line,
             "spool edit %s material={material} color={color} vendor={vendor} "
             "remaining={remaining} initial={initial} pa={pa}", slot_id);
    a = act_add(out, "edit_spool", VS_ACTION_EDIT_SPOOL, line, VIEW_STYLE_NORMAL);
    if(!a) return;

    add_edit_fields(out, a, m, s, &d, false);

    edit_resolved_line(resolved, sizeof resolved, slot_id, &d, false);
    action_check(a, resolved, m);
    /* The one form with a submit button: the row that sends it says "Save
     * edit" (the action it is here for is "Edit spool"), and a submission
     * that changed nothing is the host's own refusal to answer. */
    if(a->form.present) {
        a->form.submit_label = view_str(VS_ACTION_SAVE_EDIT);
        a->form.require_change = true;
    }
}

/* "Load" on a bay nobody has identified: the same spool editor, one choice
 * further. The form asks who is before it asks for anything else - Klipper's
 * preload table is worth nothing to a bay whose material is still "?" - and
 * the line that leaves it saves that answer and loads in one go, so the load
 * the user came for happens instead of being handed back to them. */
static void add_save_and_load_action(view_actions_t *out, const mmu_model_t *m, int unit_idx,
                                     int bay, const mmu_slot_t *s, const char *slot_id)
{
    mmu_edit_defaults_t d;
    char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
    view_action_t *a;
    size_t n;

    mmu_edit_defaults(m, unit_idx, bay, &d);
    snprintf(line, sizeof line,
             "spool edit %s material={material} color={color} vendor={vendor} "
             "remaining={remaining} initial={initial} pa={pa} then load", slot_id);
    a = act_add(out, "load", VS_ACTION_LOAD, line, VIEW_STYLE_NORMAL);
    if(!a) return;

    add_edit_fields(out, a, m, s, &d, true);

    /* The row is dimmed only while the edit cannot be taken at all (no
     * endpoint, Spoolman offline), with the reason Edit spool gives. The
     * unanswered material is not that: it is the dialog's own required field,
     * and dimming the row for it would hide the dialog that asks. So the dry
     * run is the editor's, with a material that is an answer. */
    d.material = d.material == 0 ? 1 : 0;
    edit_resolved_line(resolved, sizeof resolved, slot_id, &d, false);
    n = strlen(resolved);
    snprintf(resolved + n, sizeof resolved - n, " then load");
    action_check(a, resolved, m);
    /* ... and then the load itself has its own rules (a bay in no tool, a busy
     * lane): what would refuse the plain Load refuses this one, in its words,
     * so no spool is saved for a load that cannot follow it */
    if(a->enabled) {
        char plain[VIEW_ACTION_LINE_LEN];

        snprintf(plain, sizeof plain, "load %s", slot_id);
        action_check(a, plain, m);
    }
    /* The row that sends it says what it will do, not what it is called: a
     * "Load" row whose dialog offers "Save and load". */
    if(a->form.present) {
        a->form.submit_label = view_str(VS_SAVE_AND_LOAD);
        a->form.require_change = true;
    }
}

/* The choice the form opens on: the bay's own group, so submitting it
 * unchanged moves nothing. A bay in no group has none to name, and "No group"
 * is the one choice the mapper refuses on it - there is nothing to unassign -
 * so it takes the first group of its lane that is not the one loaded in the
 * toolhead, or "New group" when the lane holds none. "No group" and the loaded
 * group stay in the list: a user who picks them hears the host's own refusal. */
static int group_default_choice(const mmu_model_t *m, int unit_idx, int bay)
{
    int gi = mmu_slot_group_in(m, unit_idx, bay);
    int g;

    if(gi >= 0) return gi;
    for(g = 0; g < (int)m->group_count; g++) {
        if(!mmu_group_on_unit_lane(m, unit_idx, g)) continue;
        if(m->loaded && g == m->current_group) continue;
        return g;
    }
    return MMU_GROUP_CHOICE_NEW;
}

/* The "Change group..." list of a bay: every group on this unit's lane, by
 * name, then "No group" and "New group" (the next free "T<n>"). The default is
 * the bay's own group, so opening the form and submitting it unchanged moves
 * nothing. The line carries the choice as its value, and the mapper turns each
 * choice into the host command(s) it means. */
static void add_change_group_action(view_actions_t *out, const mmu_model_t *m, int unit_idx,
                                    int bay, const char *slot_id)
{
    char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
    int choice = group_default_choice(m, unit_idx, bay);
    view_action_t *a;
    view_field_t *f;
    int g;

    snprintf(line, sizeof line, "change group {group} %s", slot_id);
    a = act_add(out, "change_group", VS_HS_CHANGE_GROUP, line, VIEW_STYLE_NORMAL);
    if(!a) return;

    f = select_field(out, a, "group", VS_FIELD_GROUP_LABEL);
    if(!f) return;
    for(g = 0; g < (int)m->group_count; g++) {
        if(!mmu_group_on_unit_lane(m, unit_idx, g)) continue;
        add_option(out, f, g, m->groups[g].name);
    }
    add_option(out, f, MMU_GROUP_CHOICE_NONE, view_str(VS_MAP_NO_GROUP));
    add_option(out, f, MMU_GROUP_CHOICE_NEW, view_str(VS_GP_NEW));
    f->value = (double)choice;
    a->form.submit_label = view_str(VS_ACTION_CHANGE_GROUP);
    a->form.require_change = true;

    /* Dry-run the line the form opens on, not the bay's own group alone: the
     * destination has rules of its own (the group loaded in the toolhead), so
     * this is what the one predicate says about the change as it stands - one
     * string per refusal rule, dimmed and never hidden (PRINCIPLES.md 1). */
    snprintf(resolved, sizeof resolved, "change group %d %s", choice, slot_id);
    action_check(a, resolved, m);
}

/* A bay nobody has identified: ready, but nothing says what is in it, which is
 * what the tile's own state calls "unknown" (the tile labelled "?"). Loading it
 * asks for the spool first, so what reaches the host is an answer the user
 * chose (PRINCIPLES.md 13). */
static bool bay_is_unknown(const mmu_model_t *m, int unit_idx, int bay)
{
    view_tile_t t;

    return view_tile(m, unit_idx, bay, &t) && t.state == VIEW_TILE_UNKNOWN;
}

/* The tile wears the "Confirm" tag: a spool the host linked waits for the
 * user's word, and nothing more urgent is on the tag (a fault, a runout, a
 * motion, an empty bay) - build_tile_content()'s own rule. */
static bool bay_awaits_confirm(const mmu_model_t *m, int unit_idx, int bay)
{
    view_tile_t t;

    if(!view_tile(m, unit_idx, bay, &t) || !t.pending_confirmation) return false;
    return t.state == VIEW_TILE_LOADED || t.state == VIEW_TILE_READY || t.state == VIEW_TILE_UNKNOWN;
}

/* "Confirm spool", the "Confirm" tag's own action (UNIFIED_UI 7, SpoolConfirm):
 * how much is left on the spool the host linked, starting from a full one (its
 * initial weight, the device's "Yes, full") and adjustable before it is sent as
 * `spool confirm <slot> <grams>`. Enabled and reason are the one predicate's. */
static void add_confirm_action(view_actions_t *out, const mmu_model_t *m, int unit_idx, int bay,
                               const char *slot_id)
{
    mmu_edit_defaults_t d;
    char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
    view_action_t *a;

    mmu_edit_defaults(m, unit_idx, bay, &d);
    snprintf(line, sizeof line, "spool confirm %s {grams}", slot_id);
    a = act_add(out, "confirm_spool", VS_ACTION_CONFIRM_SPOOL, line, VIEW_STYLE_PRIMARY);
    if(!a) return;
    a->target = VIEW_TARGET_TAG;

    (void)number_field(out, a, "grams", VS_FIELD_REMAINING_LABEL, (double)d.initial_g, 0.0,
                       10000.0, 1.0, view_str(VS_FIELD_WEIGHT_UNIT));
    /* The button says what it does; confirming the full spool as it stands is
     * the common answer, so the form asks for no change. */
    if(a->form.present) a->form.submit_label = view_str(VS_BTN_CONFIRM);

    snprintf(resolved, sizeof resolved, "spool confirm %s %d", slot_id, d.initial_g);
    action_check(a, resolved, m);
}

const char *view_tile_edit_reason(const mmu_model_t *m, int unit_idx, int bay)
{
    char slot_id[VIEW_ID_LEN], resolved[VIEW_ACTION_LINE_LEN];
    const char *why = "";
    mmu_edit_defaults_t d;

    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return NULL;
    if(bay < 0 || bay >= (int)m->units[unit_idx].slot_count) return NULL;
    mmu_slot_id_in(m, unit_idx, bay, slot_id, sizeof slot_id);
    mmu_edit_defaults(m, unit_idx, bay, &d);
    /* a bay nobody has identified is saved with a material of the user's
     * choosing, never "no changes" */
    if(bay_is_unknown(m, unit_idx, bay)) d.material = d.material == 0 ? 1 : 0;
    edit_resolved_line(resolved, sizeof resolved, slot_id, &d, false);
    return actions_check(resolved, m, &why) ? NULL : why;
}

bool view_tile_actions(const mmu_model_t *m, int unit_idx, int bay, view_actions_t *out)
{
    const mmu_unit_t *u;
    const mmu_slot_t *s;
    char slot_id[VIEW_ID_LEN];
    view_action_t *a;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    u = &m->units[unit_idx];
    if(bay < 0 || bay >= (int)u->slot_count) return false;
    s = &u->slots[bay];
    mmu_slot_id_in(m, unit_idx, bay, slot_id, sizeof(slot_id));

    /* A fixed set in a fixed order; enabled and reason come from the one
     * predicate (actions_check), which dims what does not apply now. */
    if(bay_is_unknown(m, unit_idx, bay)) add_save_and_load_action(out, m, unit_idx, bay, s, slot_id);
    else act_simple(out, m, "load", VS_ACTION_LOAD, VIEW_STYLE_NORMAL, "load %s", slot_id);
    /* the bay's own unload line: the mapper resolves it to the unit's lane, and
     * only a loaded bay passes its predicate (the toolhead's card sends the lane's). */
    a = act_simple(out, m, "unload", VS_ACTION_UNLOAD, VIEW_STYLE_NORMAL, "unload %s", slot_id);
    if(a && m->printing)
        action_confirm(a, VS_CONFIRM_UNLOAD_TITLE, VS_CONFIRM_UNLOAD_TEXT, VS_CONFIRM_UNLOAD_OK);

    add_edit_action(out, m, unit_idx, bay, s, slot_id);

    {
        view_field_t *f;
        char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
        bool has_spools = false;
        int i;

        for(i = 0; i < (int)m->spool_count; i++) {
            if(m->spools[i].id != 0) { has_spools = true; break; }
        }

        if(has_spools) {
            snprintf(line, sizeof line, "link %s {spool}", slot_id);
            a = act_add(out, "assign_spool", VS_ACTION_ASSIGN_SPOOL, line, VIEW_STYLE_NORMAL);
            f = a ? form_field(out, a) : NULL;
            if(a && f) {
                vs_copy("spool", f->id, sizeof(f->id));
                f->label = view_str(VS_FIELD_SPOOL_LABEL);
                f->kind = VIEW_FIELD_SELECT;
                add_spool_options(m, s, out, f);
                snprintf(resolved, sizeof resolved, "link %s %d", slot_id, (int)f->value);
                action_check(a, resolved, m);
            }
        }
        else {
            act_simple(out, m, "spool_list_refresh", VS_ACTION_REFRESH_SPOOLS, VIEW_STYLE_NORMAL,
                       "%s", "spool list");
        }
    }

    act_simple(out, m, "unlink", VS_ACTION_UNLINK, VIEW_STYLE_NORMAL, "unlink %s", slot_id);

    add_change_group_action(out, m, unit_idx, bay, slot_id);

    /* a unit with no reader can never re-read a tag: no such action */
    if(u->rfid)
        act_simple(out, m, "reread", VS_ACTION_REREAD, VIEW_STYLE_NORMAL, "reread %s", slot_id);

    a = act_simple(out, m, "calibrate", VS_ACTION_CALIBRATE, VIEW_STYLE_NORMAL, "calibrate %s",
                   slot_id);
    if(a)
        action_confirm(a, VS_CONFIRM_CALIBRATE_TITLE, VS_CONFIRM_CALIBRATE_TEXT,
                       VS_CONFIRM_CALIBRATE_OK);

    /* Load and unload act on the tile body; everything after them edits the
     * spool (the ring), except the tag's own detail action below. */
    {
        int i;

        for(i = 0; i < (int)out->count; i++) {
            const char *id = out->items[i].id;

            out->items[i].target = (strcmp(id, "load") == 0 || strcmp(id, "unload") == 0)
                                       ? VIEW_TARGET_TILE : VIEW_TARGET_RING;
        }
    }

    /* The "Confirm" tag's details carry the confirmation itself: the tag says
     * a spool waits for the user, and its popover is where they answer. */
    if(bay_awaits_confirm(m, unit_idx, bay)) add_confirm_action(out, m, unit_idx, bay, slot_id);

    /* The tag's details: the lane fault that names this unit and bay is shown
     * by this tile (its tag says where, the message row says what), so its
     * recovery actions live here, under the tag, not in a badge. */
    if(u->toolhead >= 0 && u->toolhead < (int)m->toolhead_count) {
        const mmu_toolhead_t *th = &m->toolheads[u->toolhead];

        if(th->has_error && th->error_unit == unit_idx && th->error_slot == bay) {
            view_actions_t fa;
            int i;

            fault_actions(m, th, &fa);
            for(i = 0; i < (int)fa.count && out->count < VIEW_ACTIONS_MAX; i++) {
                out->items[out->count] = fa.items[i];
                out->items[out->count++].target = VIEW_TARGET_TAG;
            }
        }
    }

    return true;
}

/* ----------------------------------------------------------------- groups */

/* The groups that hold a bay of m->units[unit_idx], in the model's own order:
 * the group's index, or -1. @p nth asks for the @p nth'th of them. */
static int group_for_unit(const mmu_model_t *m, int unit_idx, int nth)
{
    int g, k, seen = 0;

    if(unit_idx < 0 || unit_idx >= (int)m->unit_count) return -1;
    for(g = 0; g < (int)m->group_count; g++) {
        for(k = 0; k < (int)m->groups[g].count; k++) {
            if(m->groups[g].members[k].unit != (int8_t)unit_idx) continue;
            if(seen == nth) return g;
            seen++;
            break;
        }
    }
    return -1;
}

uint8_t view_group_count(const mmu_model_t *m, int unit_idx)
{
    int nth = 0;

    if(!m) return 0;
    while(group_for_unit(m, unit_idx, nth) >= 0) nth++;
    return (uint8_t)nth;
}

/* One member row, carrying its bay's color, material and state: this unit's
 * bay named like its tile, another unit's named with its unit and dimmed - a
 * backup this unit may fall back to, never a thing to manage here, but its
 * color and state still say whether the fallback would match and be ready. */
static void build_group_member(const mmu_model_t *m, int unit_idx, int unit, int slot, bool spare,
                               view_group_member_t *out)
{
    view_tile_t t;
    bool mine = (unit == unit_idx);

    memset(out, 0, sizeof(*out));
    out->mine = mine;
    out->dim = !mine;
    out->spare = spare;
    out->state = "";
    out->state_kind = VIEW_MEMBER_STATE_NONE;
    out->state_tone = VIEW_TONE_NEUTRAL;
    if(unit < 0 || unit >= (int)m->unit_count) return;
    if(slot < 0 || slot >= (int)m->units[unit].slot_count) return;

    mmu_slot_id_in(m, unit, slot, out->slot_id, sizeof out->slot_id);
    out->bay = slot;

    if(mine) vs_spool_label(slot, out->label, sizeof out->label);
    else     vs_unit_spool(m->units[unit].name, slot, out->label, sizeof out->label);

    if(!view_tile(m, unit, slot, &t)) return;

    out->color_known = t.color_known;
    out->color = t.color;

    vs_copy(t.material, out->material, sizeof out->material);
    vs_copy(t.brand, out->brand, sizeof out->brand);
    switch(t.state) {
    case VIEW_TILE_LOADED:
        out->state = view_str(VS_GROUP_LOADED);
        out->state_kind = VIEW_MEMBER_STATE_LOADED;
        out->state_tone = VIEW_TONE_NEUTRAL;
        break;
    case VIEW_TILE_ERROR:
        out->state = view_str(VS_GROUP_ERROR);
        out->state_kind = VIEW_MEMBER_STATE_ERROR;
        out->state_tone = VIEW_TONE_ERROR;
        break;
    case VIEW_TILE_EMPTY:
        out->state = view_str(VS_GROUP_EMPTY);
        out->state_kind = VIEW_MEMBER_STATE_EMPTY;
        out->state_tone = VIEW_TONE_NEUTRAL;
        break;
    default:
        out->state = view_str(VS_GROUP_READY);
        out->state_kind = VIEW_MEMBER_STATE_READY;
        out->state_tone = VIEW_TONE_NEUTRAL;
        break;
    }
}

bool view_group(const mmu_model_t *m, int unit_idx, int index, view_group_t *out)
{
    int gi, spare_u = -1, spare_s = -1;
    const mmu_group_t *g;
    int k, shown = 0;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    out->index = -1;
    out->reason = "";
    out->editable = true;
    if(!m) return false;
    gi = group_for_unit(m, unit_idx, index);
    if(gi < 0) return false;
    g = &m->groups[gi];

    out->index = gi;
    vs_copy(g->name, out->name, sizeof out->name);
    out->reason = mmu_group_edit_reason(m, gi);
    out->editable = (out->reason[0] == '\0');

    (void)mmu_group_next_spare_in(m, gi, &spare_u, &spare_s);
    for(k = 0; k < (int)g->count && shown < VIEW_GROUP_MEMBERS_PER_GROUP; k++) {
        build_group_member(m, unit_idx, g->members[k].unit, g->members[k].slot,
                           spare_u == g->members[k].unit && spare_s == g->members[k].slot,
                           &out->members[shown++]);
    }
    out->member_count = (uint8_t)shown;
    return true;
}

bool view_group_actions(const mmu_model_t *m, int unit_idx, int index, view_actions_t *out)
{
    char line[VIEW_ACTION_LINE_LEN];
    view_action_t *a;
    int gi;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m) return false;
    gi = group_for_unit(m, unit_idx, index);
    if(gi < 0) return false;

    /* One action, always in place, dimmed with the group's own reason when the
     * host would refuse it (PRINCIPLES.md ruling 1). */
    snprintf(line, sizeof line, "delete group %s", m->groups[gi].name);
    a = act_add(out, "delete_group", VS_ACTION_DELETE_GROUP, line, VIEW_STYLE_DANGER);
    if(!a) return true;
    action_check(a, line, m);
    return true;
}

bool view_ungrouped_bays(const mmu_model_t *m, int unit_idx, view_ungrouped_t *out)
{
    const mmu_unit_t *u;
    int i;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    u = &m->units[unit_idx];
    for(i = 0; i < (int)u->slot_count && out->count < VIEW_UNGROUPED_BAYS; i++) {
        if(mmu_slot_group_in(m, unit_idx, i) >= 0) continue;
        build_group_member(m, unit_idx, unit_idx, i, false, &out->bays[out->count++]);
    }
    return true;
}

/* ------------------------------------------------------------------- unit */

static view_dryer_tone_t dryer_tone_of(mmu_dry_state_t st)
{
    switch(st) {
    case MMU_DRY_HEATING:
    case MMU_DRY_DEHUMIDIFYING: return VIEW_DRYER_HEAT;
    case MMU_DRY_COOLING:       return VIEW_DRYER_COOL;
    case MMU_DRY_ERROR:         return VIEW_DRYER_FAULT;
    case MMU_DRY_IDLE:
    default:                    return VIEW_DRYER_OFF;
    }
}

/* Mirrors screen_environment.c's own defaults exactly ("as the display's
 * environment screen does today"): the unit's own last target if it has one,
 * else 55 C, clamped to [45, the variant's ceiling]; 12 hours, or the actual
 * remaining time when a cycle is already running. */
static void dry_defaults(const mmu_unit_t *u, int *target_c, int *hours)
{
    int tmax = (u->variant == MMU_VARIANT_AMS_HT) ? 85 : 65;
    int t = (u->dry_target_c >= 45) ? u->dry_target_c : 55;

    if(t > tmax) t = tmax;
    if(t < 45) t = 45;
    *target_c = t;

    *hours = 12;
    if(u->dry_state != MMU_DRY_IDLE && u->dry_remaining_min > 0) {
        int h = (u->dry_remaining_min + 59) / 60;

        if(h < 1) h = 1;
        if(h > 24) h = 24;
        *hours = h;
    }
}

bool view_unit_actions(const mmu_model_t *m, int unit_idx, view_actions_t *out)
{
    const mmu_unit_t *u;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    u = &m->units[unit_idx];

    if(u->can_dry) {
        int target_c, hours;
        char line[VIEW_ACTION_LINE_LEN], resolved[VIEW_ACTION_LINE_LEN];
        view_action_t *a;

        dry_defaults(u, &target_c, &hours);

        snprintf(line, sizeof line, "dry %s start {target}C {hours}h", u->name);
        a = act_add(out, "dry_start", VS_ACTION_DRY_START, line, VIEW_STYLE_NORMAL);
        if(a) {
            view_field_t *ft = form_field(out, a);
            view_field_t *fh = form_field(out, a);

            /* the pool holds eight fields: two never overflow it */
            if(!ft || !fh) return true;
            vs_copy("target", ft->id, sizeof(ft->id));
            ft->label = view_str(VS_FIELD_TARGET_LABEL);
            ft->kind = VIEW_FIELD_NUMBER;
            ft->value = (double)target_c;
            ft->min = 45.0f;
            ft->max = (double)((u->variant == MMU_VARIANT_AMS_HT) ? 85 : 65);
            ft->step = 5.0f;
            ft->unit = view_str(VS_FIELD_TARGET_UNIT);

            vs_copy("hours", fh->id, sizeof(fh->id));
            fh->label = view_str(VS_FIELD_HOURS_LABEL);
            fh->kind = VIEW_FIELD_NUMBER;
            fh->value = (double)hours;
            fh->min = 1.0f;
            fh->max = 24.0f;
            fh->step = 1.0f;
            fh->unit = view_str(VS_FIELD_HOURS_UNIT);

            if(actions_dry_reason(u)[0]) {
                a->enabled = false;
                a->reason = actions_dry_reason(u);     /* the host's own words */
            }
            else {
                snprintf(resolved, sizeof resolved, "dry %s start %dC %dh", u->name, target_c,
                         hours);
                action_check(a, resolved, m);
            }
        }
    }

    if(u->can_dry)
        act_simple(out, m, "dry_stop", VS_ACTION_DRY_STOP, VIEW_STYLE_NORMAL, "dry %s stop", u->name);

    return true;
}

/* ---------------------------------------------------------------- alerts */

/* An alert's sentence: the host's own words as they are, or - for the two the
 * core raises itself, kept as numbers - the current language's template filled
 * in @p buf. */
static const char *alert_text(const mmu_model_t *m, const mmu_alert_t *a, char *buf, size_t n)
{
    char spool[64];

    switch(a->kind) {
    case MMU_ALERT_LOW_FILAMENT:
        vs_unit_spool(a->unit >= 0 && a->unit < (int)m->unit_count ? m->units[a->unit].name : "",
                      a->arg[0], spool, sizeof(spool));
        vs_low_filament(spool, a->arg[1], buf, n);
        return buf;
    case MMU_ALERT_SHORTFALL:
        vs_shortfall(a->arg[0], a->arg[1], a->arg[2], buf, n);
        return buf;
    default:
        return a->text;
    }
}


static const char *alert_title_str(mmu_severity_t sev);

/* Placement (UNIFIED_UI 4b): each alert lives on exactly one level.
 * - the live lane fault: the unit it names, else its toolhead (the history
 *   entry of a lane fault, mmu_alert_t.lane_fault, is not shown again);
 * - the dryer fault and the missing power adapter: the unit;
 * - any other history entry: its unit when it names one, else its toolhead
 *   when it names one, else the panel (the host's bar). */
static const mmu_toolhead_t *unit_lane(const mmu_model_t *m, int unit_idx)
{
    const mmu_unit_t *u = &m->units[unit_idx];

    if(u->toolhead < 0 || u->toolhead >= (int)m->toolhead_count) return NULL;
    return &m->toolheads[u->toolhead];
}

static int fault_weight(const mmu_toolhead_t *th) { return th->error_count > 0 ? th->error_count : 1; }

static void fault_item(const mmu_model_t *m, const mmu_toolhead_t *th, view_alert_t *a);
static void build_message(const mmu_model_t *m, const mmu_toolhead_t *th, view_message_t *out);

/* Whether the lane fault has any action to offer (without building them). */
static bool fault_has_actions(const mmu_model_t *m, const mmu_toolhead_t *th)
{
    int i;

    for(i = 0; i < (int)th->error_act_count; i++) {
        const char *tok = th->error_act[i];

        if(strcmp(tok, "resume") == 0 || strcmp(tok, "clear_errors") == 0) return true;
        if(strcmp(tok, "clear_fault") == 0 && th->error_unit >= 0) return true;
        if(strcmp(tok, "retry_load") == 0 &&
           ((th->current_group >= 0 && th->current_group < (int)m->group_count) ||
            (th->error_unit >= 0 && mmu_slot_group_in(m, th->error_unit, th->error_slot) >= 0)))
            return true;
    }
    return false;
}

static bool history_belongs(const mmu_model_t *m, int i, int unit_idx, int th_idx)
{
    const mmu_alert_t *a = &m->alerts[i];

    if(a->lane_fault) return false;
    if(unit_idx >= 0) return a->unit == unit_idx;
    return a->unit < 0 && a->toolhead == th_idx;
}

/* Walk the alerts of one unit (unit_idx >= 0) or one toolhead: fill the
 * summary, and the items when @p items is not NULL. */
static void scan_alerts(const mmu_model_t *m, int unit_idx, int th_idx, view_alerts_t *items,
                        view_alert_summary_t *sum)
{
    int i, count = 0, sev = (int)MMU_SEV_INFO;
    const mmu_toolhead_t *th = NULL;
    const mmu_unit_t *u = NULL;

    if(items) memset(items, 0, sizeof(*items));
    if(unit_idx >= 0) {
        u = &m->units[unit_idx];
        th = unit_lane(m, unit_idx);
    }
    else if(th_idx >= 0 && th_idx < (int)m->toolhead_count) {
        th = &m->toolheads[th_idx];
    }

#define ADD_ITEM(weight, severity_v) do { \
        count += (weight); if((int)(severity_v) > sev) sev = (int)(severity_v); \
    } while(0)
#define NEXT_ITEM() ((items && items->count < VIEW_ALERTS_PER_GROUP) ? &items->items[items->count++] : NULL)

    /* A badge lists only what is not already shown elsewhere, or carries
     * actions (UNIFIED_UI 4c):
     * (a) a lane fault that names a unit and a bay is on that tile (its tag
     *     says where, the message row what) and its actions are the tag's;
     * (c) a lane fault that is the message row's text and offers no action. */
    if(th && th->has_error && (unit_idx >= 0 ? th->error_unit == unit_idx : th->error_unit < 0) &&
       !(th->error_unit >= 0 && th->error_slot >= 0) && fault_has_actions(m, th)) {
        view_alert_t *a = NEXT_ITEM();

        if(a) fault_item(m, th, a);
        ADD_ITEM(fault_weight(th), th->error_severity);
    }
    else if(th && th->has_error && (unit_idx >= 0 ? th->error_unit == unit_idx : th->error_unit < 0) &&
            !(th->error_unit >= 0 && th->error_slot >= 0)) {
        /* no actions: only listed when the message row shows something else */
        view_message_t msg;

        build_message(m, th, &msg);
        if(strcmp(msg.text, th->error_text[0] ? th->error_text : th->error_code) != 0) {
            view_alert_t *a = NEXT_ITEM();

            if(a) fault_item(m, th, a);
            ADD_ITEM(fault_weight(th), th->error_severity);
        }
    }
    if(u && u->can_dry && u->dry_state == MMU_DRY_ERROR) {
        view_alert_t *a = NEXT_ITEM();

        if(a) {
            memset(a, 0, sizeof(*a));
            a->severity = MMU_SEV_PAUSE;
            a->title = alert_title_str(MMU_SEV_PAUSE);
            a->text = view_str(VS_ALERT_DRYER);
            a->has_unit = true;
            vs_copy(u->name, a->unit, sizeof(a->unit));
        }
        ADD_ITEM(1, MMU_SEV_PAUSE);
    }
    if(u && u->can_dry && u->has_power_adapter && !u->power_adapter) {
        view_alert_t *a = NEXT_ITEM();

        if(a) {
            memset(a, 0, sizeof(*a));
            a->severity = MMU_SEV_INFO;
            a->title = alert_title_str(MMU_SEV_INFO);
            a->text = view_str(VS_ALERT_ADAPTER);
            a->has_unit = true;
            vs_copy(u->name, a->unit, sizeof(a->unit));
        }
        ADD_ITEM(1, MMU_SEV_INFO);
    }
    for(i = 0; i < (int)m->alert_count; i++) {
        view_alert_t *a;

        if(!history_belongs(m, i, unit_idx, th_idx)) continue;
        /* a history entry leaves the badge once it has aged out (4g); it stays
         * in the alerts history */
        if(mmu_alert_badge_expired(m, &m->alerts[i])) continue;
        /* (b) low filament (a unit's history entry): the tile's low ring says it.
         * (c) a toolhead's entry (the job shortfall): skipped while the message
         * row shows the same text, and it has no action. */
        if(unit_idx >= 0) continue;
        if(th) {
            view_message_t msg;

            char abuf[VIEW_ALERT_TEXT_LEN];

            build_message(m, th, &msg);
            if(strcmp(msg.text, alert_text(m, &m->alerts[i], abuf, sizeof abuf)) == 0) continue;
        }
        a = NEXT_ITEM();
        if(a) view_alert(m, i, a);
        ADD_ITEM(1, m->alerts[i].severity);
    }
#undef ADD_ITEM
#undef NEXT_ITEM

    memset(sum, 0, sizeof(*sum));
    sum->present = count > 0;
    sum->count = count;
    sum->severity = sev;
    /* the two tones of the message row: red for a fault or a pause, else yellow */
    sum->tone = (sev >= (int)MMU_SEV_PAUSE) ? VIEW_TONE_ERROR : VIEW_TONE_INFO;
    if(items) { items->severity = sev; items->total = count; items->tone = sum->tone; }
}

/* Whether nothing in a unit's scope alerts, shown or not: the badge, a lane
 * fault or a runout naming the unit, a faulted bay, a history entry of the
 * unit (low filament included, though its tile says it) that is still
 * unread: one the user has read no longer needs attention. */
static bool unit_scope_clear(const mmu_model_t *m, int unit_idx, const view_alert_summary_t *sum)
{
    const mmu_unit_t *u = &m->units[unit_idx];
    const mmu_toolhead_t *th = unit_lane(m, unit_idx);
    int i;

    if(sum->present) return false;
    if(th && th->has_error && th->error_unit == unit_idx) return false;
    if(th && th->runout_active && th->runout_from_unit == unit_idx) return false;
    for(i = 0; i < (int)u->slot_count; i++)
        if(u->slots[i].state == MMU_SLOT_ERROR) return false;
    for(i = 0; i < (int)m->alert_count; i++)
        if(!m->alerts[i].lane_fault && m->alerts[i].unread && m->alerts[i].unit == unit_idx &&
           !mmu_alert_badge_expired(m, &m->alerts[i])) return false;
    return true;
}

static void unit_alert_summary(const mmu_model_t *m, int unit_idx, view_alert_summary_t *out)
{
    scan_alerts(m, unit_idx, -1, NULL, out);
    out->clear = unit_scope_clear(m, unit_idx, out);
}

/* The same for a toolhead (a lane): its badge, its fault or runout, anything
 * its message row is saying in a tone, and its own unread history entries. */
static bool toolhead_scope_clear(const mmu_model_t *m, int th_idx, const mmu_toolhead_t *th,
                                 const view_alert_summary_t *sum, const view_message_t *msg)
{
    int i;

    if(sum->present || th->has_error || th->runout_active) return false;
    if(msg->tone != VIEW_TONE_NEUTRAL) return false;
    for(i = 0; i < (int)m->alert_count; i++)
        if(!m->alerts[i].lane_fault && m->alerts[i].unread && m->alerts[i].unit < 0 &&
           m->alerts[i].toolhead == th_idx && !mmu_alert_badge_expired(m, &m->alerts[i])) return false;
    return true;
}

/* The actions a lane fault's support tokens offer, in the host's order (the
 * fault's first two tokens): retry, resume, clear errors, and the unit-scoped
 * clear fault when the fault names a unit. */
static void fault_actions(const mmu_model_t *m, const mmu_toolhead_t *th, view_actions_t *out)
{
    view_action_t *a;
    int i;

    memset(out, 0, sizeof(*out));
    for(i = 0; i < (int)th->error_act_count; i++) {
        const char *tok = th->error_act[i];

        if(strcmp(tok, "retry_load") == 0) {
            const char *gname = NULL;

            if(th->current_group >= 0 && th->current_group < (int)m->group_count)
                gname = m->groups[th->current_group].name;
            else if(th->error_unit >= 0) {
                int gi = mmu_slot_group_in(m, th->error_unit, th->error_slot);
                if(gi >= 0) gname = m->groups[gi].name;
            }
            if(!gname) continue;
            act_simple(out, m, "retry_load", VS_ACTION_RETRY_LOAD, VIEW_STYLE_PRIMARY,
                       "fault retry_load %s", gname);
        }
        else if(strcmp(tok, "resume") == 0) {
            act_simple(out, m, "resume", VS_ACTION_RESUME, VIEW_STYLE_PRIMARY, "%s", "fault resume");
        }
        else if(strcmp(tok, "clear_errors") == 0) {
            act_simple(out, m, "clear_errors", VS_ACTION_CLEAR_ERRORS, VIEW_STYLE_DANGER, "%s",
                       "fault clear_errors");
        }
        else if(strcmp(tok, "clear_fault") == 0 && th->error_unit >= 0 &&
                th->error_unit < (int)m->unit_count) {
            a = act_simple(out, m, "clear_fault", VS_ACTION_CLEAR_FAULT, VIEW_STYLE_DANGER,
                           "fault clear_fault %s", m->units[th->error_unit].name);
            if(a)
                action_confirm(a, VS_CONFIRM_CLEAR_FAULT_TITLE, VS_CONFIRM_CLEAR_FAULT_TEXT,
                               VS_CONFIRM_CLEAR_FAULT_OK);
        }
    }
}

/* One lane fault as an alert item, with the actions its tokens offer. */
static void fault_item(const mmu_model_t *m, const mmu_toolhead_t *th, view_alert_t *a)
{
    view_actions_t acts;
    int i;

    memset(a, 0, sizeof(*a));
    a->severity = th->error_severity;
    vs_copy(th->error_code, a->code, sizeof(a->code));
    a->title = alert_title_str(th->error_severity);
    /* the host's own words, borrowed from the model */
    a->text = th->error_text[0] ? th->error_text : th->error_code;
    if(th->error_unit >= 0 && th->error_unit < (int)m->unit_count) {
        a->has_unit = true;
        vs_copy(m->units[th->error_unit].name, a->unit, sizeof(a->unit));
        if(th->error_slot >= 0) {
            a->has_slot = true;
            mmu_slot_id_in(m, th->error_unit, th->error_slot, a->slot_id, sizeof(a->slot_id));
        }
    }
    a->unread = true;

    a->action_count = 0;
    fault_actions(m, th, &acts);
    for(i = 0; i < (int)acts.count && i < VIEW_ACTIONS_PER_ALERT; i++)
        a->actions[a->action_count++] = acts.items[i];
}

bool view_toolhead_alerts(const mmu_model_t *m, int toolhead_idx, view_alerts_t *out)
{
    view_alert_summary_t sum;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || toolhead_idx < 0 || toolhead_idx >= (int)m->toolhead_count) return false;
    scan_alerts(m, -1, toolhead_idx, out, &sum);
    return true;
}

bool view_unit_alerts(const mmu_model_t *m, int unit_idx, view_alerts_t *out)
{
    view_alert_summary_t sum;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    scan_alerts(m, unit_idx, -1, out, &sum);
    return true;
}

bool view_unit(const mmu_model_t *m, int unit_idx, view_unit_t *out)
{
    const mmu_unit_t *u;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || unit_idx < 0 || unit_idx >= (int)m->unit_count) return false;
    u = &m->units[unit_idx];

    out->title = mmu_unit_title(u);
    vs_copy(u->name, out->subtitle, sizeof(out->subtitle));
    out->online = u->connected;
    out->status_text = "";
    if(!u->connected) out->status_text = view_str(VS_UNIT_OFFLINE);

    /* A missing source is unknown, never a possibly wrong value: an offline unit
     * shows "Offline" (status_text) in place of its reading, and has no dryer pill. */
    if(u->connected && (u->has_humidity_pct || u->has_temp)) {
        out->env.present = true;
        vs_env(u->has_humidity_pct, u->humidity_pct, u->has_temp, u->temp_c,
                   out->env.text, sizeof(out->env.text));
    }

    if(u->can_dry && u->connected) {
        out->dryer.present = true;
        out->dryer.tone = dryer_tone_of(u->dry_state);
        /* only a host that reported the supply can say it is missing: the AMS HT
         * has no monitor, so its adapter is unknown, not absent. */
        out->dryer.adapter_missing = u->has_power_adapter && !u->power_adapter;
        switch(out->dryer.tone) {
        case VIEW_DRYER_HEAT:
            vs_dryer_active(u->dry_target_c, u->dry_remaining_min,
                                out->dryer.text, sizeof(out->dryer.text));
            break;
        case VIEW_DRYER_COOL:
            vs_copy(view_str(VS_DRYER_COOLING), out->dryer.text, sizeof(out->dryer.text));
            break;
        case VIEW_DRYER_FAULT:
            out->dryer.text[0] = '\0';     /* a dryer fault is an item of the unit's alert */
            break;
        default:
            out->dryer.text[0] = '\0';
            break;
        }
    }

    unit_alert_summary(m, unit_idx, &out->alert);

    out->info.present = true;
    out->info.family = mmu_unit_title(u);
    /* serial/firmware: no real-host field carries these yet (mmu_unit_ident()
     * is the demo's own fabricated stand-in, not live data): view_json emits
     * the empty string for both, rather than an invented value. */

    out->bay_count = u->slot_count;
    return true;
}

/* -------------------------------------------------------------- toolhead */

/* The display label of one H2 stage step name (docs/design/H2_STAGE_MODEL.md
 * 4), in the current language; a name newer than this list, or one that is
 * already a sentence, shows as itself. A step the host gave a label of its own
 * arrives here already spelled out (H3), so it falls through to that last
 * case: the user's words, never translated. */
static const char *stage_label(const char *name)
{
    static const struct {
        const char *name;
        view_str_id_t label;
    } k_labels[] = {
        { "heat",      VS_STEP_HEAT },
        { "cut",       VS_STEP_CUT },
        { "retract",   VS_STEP_RETRACT },
        { "feed",      VS_STEP_FEED },
        { "purge",     VS_STEP_PURGE },
        { "grab",      VS_STEP_GRAB },
        { "calibrate", VS_STEP_CALIBRATE },
        { "home",      VS_STEP_HOME },
        { "clean",     VS_STEP_CLEAN },
    };
    size_t i;

    for(i = 0; i < sizeof(k_labels) / sizeof(k_labels[0]); i++) {
        if(strcmp(k_labels[i].name, name) == 0) return view_str(k_labels[i].label);
    }
    return name;
}

/* The tools a toolhead could load right now: the group names (the labels the
 * hotend icon overlays), in group order, that have at least one ready bay on
 * one of this toolhead's connected units. Whole names only: a name that no
 * longer fits stops the list rather than being cut mid-glyph. Returns false
 * when no bay is ready. */
static bool ready_tools(const mmu_model_t *m, int th_idx, char *out, size_t n)
{
    int gi, mi;
    bool any = false;

    out[0] = '\0';
    for(gi = 0; gi < (int)m->group_count; gi++) {
        const mmu_group_t *g = &m->groups[gi];
        bool ready = false;

        for(mi = 0; mi < (int)g->count && !ready; mi++) {
            int ui = g->members[mi].unit, si = g->members[mi].slot;
            const mmu_unit_t *un;

            if(ui < 0 || ui >= (int)m->unit_count) continue;
            un = &m->units[ui];
            if(un->toolhead != th_idx || !un->connected) continue;
            if(si < 0 || si >= (int)un->slot_count) continue;
            ready = un->slots[si].state == MMU_SLOT_READY;
        }
        if(!ready) continue;
        if(any) {
            size_t len = strlen(out);
            const char *sep = view_str(VS_REST_JOIN);

            if(len + strlen(sep) + strlen(g->name) >= n) break;
            memcpy(out + len, sep, strlen(sep) + 1);
        }
        else if(strlen(g->name) >= n) break;
        strcat(out, g->name);
        any = true;
    }
    return any;
}

/* Nothing is loaded: say what could be, "Ready: T0 \xc2\xb7 T1", or the next
 * step, "Insert a spool", when no bay is ready (principle 4: the message row
 * already says nothing is loaded). */
static void build_rest_empty(const mmu_model_t *m, const mmu_toolhead_t *th, view_activity_t *out)
{
    char tools[96];

    if(ready_tools(m, (int)(th - m->toolheads), tools, sizeof(tools))) {
        const vs_arg_t args[1] = { VS_ARG_T("tools", tools) };

        vs_format(VS_REST_READY, out->rest.label, sizeof(out->rest.label), args, 1);
    }
    else {
        vs_copy(view_str(VS_REST_INSERT), out->rest.label, sizeof(out->rest.label));
    }
}

/* The slot's resting state: the toolhead's own path in the stepper's form,
 * shown while no plan runs (PRINCIPLES.md 2, "A reserved slot has a resting
 * state"). A bay that is feeding it names the tool, the bay and the material
 * (REST_PATH, or REST_PATH_NO_MATERIAL when nobody told us the material);
 * nothing from a bay rests as REST_READY or REST_INSERT. Only the composition happens here:
 * a renderer draws this segment and nothing else (the device's home has no
 * stepper and draws none, UNIFIED_UI 4d).
 *
 * An external spool (th->loaded_ext >= 0) has no bay to name, so its path
 * rests empty too, the same as no filament at all. */
static void build_activity_rest(const mmu_model_t *m, const mmu_toolhead_t *th,
                                view_activity_t *out)
{
    const mmu_unit_t *u;
    const mmu_slot_t *s;
    const char *tool;
    char spool[VIEW_SUBLABEL_LEN];
    vs_arg_t args[3];
    view_str_id_t id;
    int gi;

    out->rest.present = true;
    if(!th->loaded || th->loaded_ext >= 0 || th->loaded_unit < 0 ||
       th->loaded_unit >= (int)m->unit_count) {
        build_rest_empty(m, th, out);
        return;
    }

    u = &m->units[th->loaded_unit];
    if(th->loaded_slot < 0 || th->loaded_slot >= (int)u->slot_count) {
        build_rest_empty(m, th, out);
        return;
    }
    s = &u->slots[th->loaded_slot];

    /* The tool label is the one the hotend icon overlays: the group's name, or
     * "?" for a bay in no group. */
    gi = mmu_slot_group_in(m, th->loaded_unit, th->loaded_slot);
    tool = (gi >= 0) ? m->groups[gi].name : view_str(VS_UNKNOWN_MARK);
    vs_unit_bay_label(u->name, th->loaded_slot, spool, sizeof(spool));

    args[0] = (vs_arg_t)VS_ARG_T("tool", tool);
    args[1] = (vs_arg_t)VS_ARG_T("spool", spool);
    if(s->material[0]) {
        args[2] = (vs_arg_t)VS_ARG_T("material", s->material);
        id = VS_REST_PATH;
    }
    else {
        id = VS_REST_PATH_NO_MATERIAL;
    }
    vs_format(id, out->rest.label, sizeof(out->rest.label), args, s->material[0] ? 3 : 2);

    out->rest.loaded = true;
    out->rest.color_known = s->color_known;
    if(s->color_known) {
        out->rest.color = s->color;
        out->rest.outline = outline_for(s->color);
    }
}

static void build_activity(const mmu_model_t *m, const mmu_toolhead_t *th, view_activity_t *out)
{
    int i, n;

    memset(out, 0, sizeof(*out));
    if(th->has_error)                     out->kind = VIEW_ACTIVITY_ERROR;
    else if(th->runout_active)            out->kind = VIEW_ACTIVITY_RUNOUT;
    else if(th->busy == MMU_BUSY_LOAD)    out->kind = VIEW_ACTIVITY_LOADING;
    else if(th->busy == MMU_BUSY_UNLOAD)  out->kind = VIEW_ACTIVITY_UNLOADING;
    else                                  out->kind = VIEW_ACTIVITY_IDLE;

    n = th->step_count;
    if(n > MMU_MAX_STEPS) n = MMU_MAX_STEPS;
    for(i = 0; i < n; i++) out->steps[i] = stage_label(th->steps[i]);
    out->step_count = (uint8_t)n;
    out->index = th->step_current;
    out->failed = th->step_failed;

    /* The running step carries its own detail beside its text ("250 °C", H3),
     * and only it does; the rest of the plan is drawn exactly as before. The
     * composition is the view's, so every renderer draws activity.steps[] as
     * it always has. */
    if(th->step_detail[0] && out->index >= 0 && out->index < n) {
        const vs_arg_t args[2] = {
            VS_ARG_T("step", out->steps[out->index]),
            VS_ARG_T("detail", th->step_detail),
        };

        vs_format(VS_STEP_DETAIL, out->detail_text, sizeof(out->detail_text), args, 2);
        out->steps[out->index] = out->detail_text;
    }

    /* With no plan there is nothing to advance, and the slot rests on the
     * toolhead's own path instead (principle 2). */
    if(n == 0) build_activity_rest(m, th, out);
}

/* The tool name of a runout's slot ("T0"); "?" when its bay is in no group. */
static const char *runout_tool(const mmu_model_t *m, int unit, int slot, int *gi_out)
{
    int gi = (unit >= 0 && slot >= 0) ? mmu_slot_group_in(m, unit, slot) : -1;

    if(gi_out) *gi_out = gi;
    return (gi >= 0) ? m->groups[gi].name : view_str(VS_UNKNOWN_MARK);
}

/* "T0 ran out; switching to Spool 2" (the target is the same tool) or "... to
 * T1 (Spool 2)"; just "T0 ran out" when the target is not known yet. */
static bool runout_text(const mmu_model_t *m, const mmu_toolhead_t *th, char *out, size_t n)
{
    int from_gi, to_gi;
    const char *from = runout_tool(m, th->runout_from_unit, th->runout_from_slot, &from_gi);
    char to[VIEW_LABEL_LEN * 3] = "";

    if(th->runout_from_unit < 0 && th->runout_note[0]) {
        vs_copy(th->runout_note, out, n);       /* the host's own words, no slot to name */
        return false;
    }
    if(th->runout_from_unit < 0) {
        vs_copy(view_str(VS_STATUS_RUNOUT), out, n);
        return false;
    }
    if(th->runout_to_unit >= 0 && th->runout_to_unit < (int)m->unit_count && th->runout_to_slot >= 0) {
        const char *to_tool = runout_tool(m, th->runout_to_unit, th->runout_to_slot, &to_gi);
        const char *to_unit = m->units[th->runout_to_unit].name;

        if(to_gi >= 0 && to_gi == from_gi) vs_unit_spool(to_unit, th->runout_to_slot, to, sizeof to);
        else vs_tool_spool(to_tool, to_unit, th->runout_to_slot, to, sizeof to);
    }
    /* no target and nothing left to fall back to: say so plainly (an error) */
    if(!to[0] && from_gi >= 0) {
        int su, ss;

        if(!mmu_group_next_spare_in(m, from_gi, &su, &ss)) {
            vs_runout_no_spare(from, out, n);
            return true;
        }
    }
    vs_runout(from, to, out, n);
    return false;
}

/* One message line: an error > a runout > a notice > the plain status. */
static void build_message(const mmu_model_t *m, const mmu_toolhead_t *th, view_message_t *out)
{
    memset(out, 0, sizeof(*out));

    if(th->has_error) {
        out->tone = VIEW_TONE_ERROR;
        vs_copy(th->error_text[0] ? th->error_text : th->error_code, out->text, sizeof(out->text));
        return;
    }
    if(th->runout_active) {
        out->tone = runout_text(m, th, out->text, sizeof(out->text)) ? VIEW_TONE_ERROR : VIEW_TONE_INFO;
        return;
    }
    if(th->current_group >= 0 && th->current_group < (int)m->group_count) {
        int tool_index = group_tool_index(m->groups[th->current_group].name);
        int need = 0, have = 0;

        if(tool_index >= 0 && mmu_model_tool_shortfall(m, tool_index, &need, &have)) {
            out->tone = VIEW_TONE_INFO;
            vs_shortfall(tool_index, need, have, out->text, sizeof(out->text));
            return;
        }
    }
    /* A tile in error that no lane fault covers: the tag says where, this row
     * says what, so the error is visible without a tap (PRINCIPLES.md 10). A
     * load or unload in progress keeps its own step message. */
    if(th->busy == MMU_BUSY_NONE) {
        int ui, si;
        int th_idx = (int)(th - m->toolheads);

        for(ui = 0; ui < (int)m->unit_count; ui++) {
            const mmu_unit_t *un = &m->units[ui];

            if(un->toolhead != th_idx || !un->connected) continue;
            for(si = 0; si < (int)un->slot_count; si++) {
                char who[VIEW_MESSAGE_TEXT_LEN];

                if(un->slots[si].state != MMU_SLOT_ERROR) continue;
                vs_unit_spool(un->name, si, who, sizeof(who));
                vs_spool_error(who, out->text, sizeof(out->text));
                out->tone = VIEW_TONE_ERROR;
                return;
            }
        }
    }
    out->tone = VIEW_TONE_NEUTRAL;
    if(th->busy == MMU_BUSY_LOAD)        vs_copy(view_str(VS_STATUS_LOADING), out->text, sizeof(out->text));
    else if(th->busy == MMU_BUSY_UNLOAD) vs_copy(view_str(VS_STATUS_UNLOADING), out->text, sizeof(out->text));
    else if(th->loaded)                  vs_copy(view_str(VS_STATUS_LOADED), out->text, sizeof(out->text));
    else                                 vs_copy(view_str(VS_STATUS_NO_FILAMENT), out->text, sizeof(out->text));
}

bool view_toolhead_actions(const mmu_model_t *m, int toolhead_idx, view_actions_t *out)
{
    const mmu_toolhead_t *th;
    view_action_t *a;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || toolhead_idx < 0 || toolhead_idx >= (int)m->toolhead_count) return false;
    th = &m->toolheads[toolhead_idx];

    /* Unload and Stop, always, in this order; the predicate dims the one that does not apply. */
    a = act_simple(out, m, "unload", VS_ACTION_UNLOAD, VIEW_STYLE_NORMAL, "unload %s", th->id);
    if(a && m->printing)
        action_confirm(a, VS_CONFIRM_UNLOAD_TITLE, VS_CONFIRM_UNLOAD_TEXT, VS_CONFIRM_UNLOAD_OK);

    a = act_simple(out, m, "stop", VS_ACTION_STOP, VIEW_STYLE_NORMAL, "stop %s", th->id);
    if(a)
        action_confirm(a, VS_CONFIRM_STOP_TITLE, VS_CONFIRM_STOP_TEXT, VS_CONFIRM_STOP_OK);

    return true;
}

bool view_toolhead(const mmu_model_t *m, int toolhead_idx, view_toolhead_t *out)
{
    const mmu_toolhead_t *th;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || toolhead_idx < 0 || toolhead_idx >= (int)m->toolhead_count) return false;
    th = &m->toolheads[toolhead_idx];

    /* Toolhead = extruder: the title is the extruder's display name, the FPS id
     * (as configured) sits under it (UNIFIED_UI 4a). The id is the model's own
     * string, borrowed rather than copied into two buffers. */
    if(th->extruder[0]) vs_extruder_title(th->extruder, out->title, sizeof(out->title));
    else vs_copy(view_str(VS_TOOLHEAD_DEFAULT_TITLE), out->title, sizeof(out->title));
    out->id = th->id;
    out->subtitle = th->id;

    build_activity(m, th, &out->activity);
    build_message(m, th, &out->message);
    scan_alerts(m, -1, toolhead_idx, NULL, &out->alert);
    out->alert.clear = toolhead_scope_clear(m, toolhead_idx, th, &out->alert, &out->message);

    /* The loaded tool: the hotend icon's overlay and tint only. */
    if(th->loaded) {
        int gi = (th->loaded_unit >= 0) ? mmu_slot_group_in(m, th->loaded_unit, th->loaded_slot) : -1;

        out->tool.present = true;
        if(gi >= 0) vs_copy(m->groups[gi].name, out->tool.label, sizeof(out->tool.label));
        else vs_copy(view_str(VS_UNKNOWN_MARK), out->tool.label, sizeof(out->tool.label));

        if(th->loaded_ext < 0 && th->loaded_unit >= 0 && th->loaded_unit < (int)m->unit_count) {
            const mmu_unit_t *lu = &m->units[th->loaded_unit];

            if(th->loaded_slot >= 0 && th->loaded_slot < (int)lu->slot_count &&
               lu->slots[th->loaded_slot].color_known) {
                out->tool.color_known = true;
                out->tool.color = lu->slots[th->loaded_slot].color;
            }
        }
        out->tool.ink = ink_for(out->tool.color);
        out->tool.outline = out->tool.color_known && outline_for(out->tool.color);
    }

    if(th->pressure >= 0.0f) {
        out->pressure.present = true;
        out->pressure.value = th->pressure;
        out->pressure.set_point = th->set_point;
        out->pressure.has_band = (th->band_low >= 0.0f && th->band_high > th->band_low &&
                                  th->band_high <= 1.0f);
        if(out->pressure.has_band) {
            out->pressure.band_low = th->band_low;
            out->pressure.band_high = th->band_high;
        }
        out->pressure.label = view_str(VS_LABEL_PRESSURE);
        vs_pressure(th->pressure, out->pressure.text, sizeof(out->pressure.text));
        vs_scale(0.0f, out->pressure.scale[0], VIEW_SCALE_LEN);
        vs_scale(th->set_point, out->pressure.scale[1], VIEW_SCALE_LEN);
        vs_scale(1.0f, out->pressure.scale[2], VIEW_SCALE_LEN);
    }

    return true;
}

/* ------------------------------------------------------------------ alert */

static const char *alert_title_str(mmu_severity_t sev)
{
    switch(sev) {
    case MMU_SEV_STOP:  return view_str(VS_ALERT_TITLE_STOP);
    case MMU_SEV_PAUSE: return view_str(VS_ALERT_TITLE_PAUSE);
    case MMU_SEV_INFO:
    default:            return view_str(VS_ALERT_TITLE_INFO);
    }
}

bool view_alert(const mmu_model_t *m, int alert_idx, view_alert_t *out)
{
    const mmu_alert_t *a;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m || alert_idx < 0 || alert_idx >= (int)m->alert_count) return false;
    a = &m->alerts[alert_idx];

    out->severity = a->severity;
    /* code, slot_id, when_text: mmu_alert_t carries none of these yet (only
     * text/severity/unit/unread) - left "" rather than fabricated. */
    out->title = alert_title_str(a->severity);
    out->text = alert_text(m, a, out->text_buf, sizeof(out->text_buf));
    if(a->unit >= 0 && a->unit < (int)m->unit_count) {
        out->has_unit = true;
        vs_copy(m->units[a->unit].name, out->unit, sizeof(out->unit));
    }
    out->unread = a->unread;
    /* actions: mmu_alert_t's history entries carry no claim/action data of
     * their own (only the *current* active fault, on the toolhead, does) -
     * left empty. */
    out->action_count = 0;

    return true;
}

/* ------------------------------------------------------- the whole-panel list */

/* One row of the list: @p a's own label, line and reason, flattened. A list has
 * no form and no confirm, so a row is all a sheet needs. */
static void row_add(view_action_rows_t *o, const view_action_t *a)
{
    view_action_row_t *r;

    if(!a || o->count >= VIEW_LIST_MAX) return;
    r = &o->rows[o->count++];
    memset(r, 0, sizeof(*r));
    vs_copy(a->id, r->id, sizeof(r->id));
    r->label = a->label;
    vs_copy(a->line, r->line, sizeof(r->line));
    r->reason = a->reason;
}

/* Every action of @p a, in order. */
static void rows_add_all(view_action_rows_t *o, const view_actions_t *a)
{
    int i;

    for(i = 0; i < (int)a->count; i++) row_add(o, &a->items[i]);
}

/* The bay's load or unload - whichever applies - under a label naming the bay,
 * so a list of several bays' rows says whose is whose. The rest of the bay's
 * actions keep their own labels. */
static void row_add_bay_io(view_action_rows_t *o, const view_action_t *a, int unit_idx, int bay)
{
    view_action_row_t *r;

    if(!a) return;
    row_add(o, a);
    r = &o->rows[o->count - 1];
    vs_unit_spool(mmu_unit_name(unit_idx), bay, r->label_buf, sizeof r->label_buf);
    r->label = r->label_buf;
}

bool view_list_actions(const mmu_model_t *m, view_action_rows_t *out)
{
    view_actions_t a;
    int unit, i;

    if(!out) return false;
    memset(out, 0, sizeof(*out));
    if(!m) return false;

    /* The bound unit's bays first: what can be loaded or unloaded, and what can
     * be said about a spool, in the order the tiles are read. The assign and
     * edit rows need a sheet of their own, so they carry no line a list could
     * send - the list opens them by their id instead. */
    unit = mmu_this_unit();
    if(unit >= 0 && unit < (int)m->unit_count) {
        for(i = 0; i < (int)m->units[unit].slot_count; i++) {
            int k;

            if(!view_tile_actions(m, unit, i, &a)) continue;
            for(k = 0; k < (int)a.count; k++) {
                const char *id = a.items[k].id;

                if(strcmp(id, "load") == 0 || strcmp(id, "unload") == 0)
                    row_add_bay_io(out, &a.items[k], unit, i);
                else if(strcmp(id, "edit_spool") == 0 || strcmp(id, "assign_spool") == 0 ||
                        strcmp(id, "spool_list_refresh") == 0 || strcmp(id, "reread") == 0)
                    row_add(out, &a.items[k]);
            }
        }
    }

    /* The toolhead's own: unload and stop. */
    for(i = 0; i < (int)m->toolhead_count; i++) {
        if(!view_toolhead_actions(m, i, &a)) continue;
        rows_add_all(out, &a);
    }

    /* "Pause and load", for whatever the lane is holding. */
    for(i = 0; i < (int)m->toolhead_count; i++) {
        const mmu_toolhead_t *th = &m->toolheads[i];
        char id[VIEW_ID_LEN];

        if(th->loaded_unit < 0 || th->loaded_unit >= (int)m->unit_count || th->loaded_slot < 0 ||
           th->loaded_slot >= (int)m->units[th->loaded_unit].slot_count)
            continue;
        mmu_slot_id(th->loaded_unit, th->loaded_slot, id, sizeof id);
        act_simple(&a, m, "pause_and_load", VS_PRINT_PAUSE_LOAD, VIEW_STYLE_PRIMARY,
                   "pause and load %s", id);
        if(a.count) row_add(out, &a.items[0]);
        break;
    }

    /* The host's settings: one row per switch, off when it is on and on when
     * it is off - the row says what tapping it does. */
    {
        view_panel_t p;

        view_panel(m, &p);
        for(i = 0; i < (int)p.setting_count; i++) {
            const view_setting_t *s = &p.settings[i];

            if(s->is_choice) continue;      /* the language row opens a list of its own */
            row_add(out, s->value ? &s->action_off : &s->action_on);
            if(out->count) out->rows[out->count - 1].label = s->label;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ panel */

void view_panel(const mmu_model_t *m, view_panel_t *out)
{
    int i;
    uint8_t n;

    if(!out) return;
    memset(out, 0, sizeof(*out));
    if(!m) return;

    out->toolhead_count = m->toolhead_count;
    for(i = 0; i < (int)m->unit_count && out->unassigned_count < VIEW_MAX_UNASSIGNED; i++) {
        if(m->units[i].toolhead < 0)
            out->unassigned_units[out->unassigned_count++] = (uint8_t)i;
    }

    for(i = 0; i < (int)m->alert_count && out->alert_count < VIEW_MAX_ALERTS; i++) {
        if(m->alerts[i].unit >= 0 || m->alerts[i].toolhead >= 0 || m->alerts[i].lane_fault)
            continue;                           /* a unit's or a toolhead's own: shown there */
        out->alert_idx[out->alert_count++] = (uint8_t)i;
        if(m->alerts[i].unread) out->unread_count++;
    }

    if(m->unit_count == 0) {
        out->notice.present = true;
        out->notice.text = view_str(VS_PANEL_NO_UNITS);
    }
    else {
        bool any_connected = false;

        for(i = 0; i < (int)m->unit_count; i++) {
            if(m->units[i].connected) { any_connected = true; break; }
        }
        if(!any_connected) {
            out->notice.present = true;
            out->notice.text = view_str(VS_PANEL_HOST_OFFLINE);
        }
        else if(!m->settings.known) {
            out->notice.present = true;
            out->notice.text = view_str(VS_PANEL_NOT_READY);
        }
    }

    /* The empty list's own words: a list with nothing in it has no load under
     * way anywhere to report on, so it says so rather than showing a blank. */
    if(m->busy_unit < 0 && !m->loaded) {
        out->nothing_to_load.present = true;
        out->nothing_to_load.text = view_str(VS_NOTICE_NOTHING_TO_LOAD);
    }

    out->labels.alerts = view_str(VS_LABEL_ALERTS);
    out->labels.settings = view_str(VS_LABEL_SETTINGS);
    out->labels.fault = view_str(VS_LABEL_FAULT);
    out->labels.pressure = view_str(VS_LABEL_PRESSURE);
    out->labels.no_alerts = view_str(VS_LABEL_NO_ALERTS);
    out->labels.close = view_str(VS_LABEL_CLOSE);
    out->labels.cancel = view_str(VS_LABEL_CANCEL);
    out->labels.no_response = view_str(VS_LABEL_NO_RESPONSE);
    out->labels.edit_failed = view_str(VS_LABEL_EDIT_FAILED);
    out->labels.action_refused = view_str(VS_ACTION_REFUSED);
    out->labels.groups = view_str(VS_SCREEN_GROUPS);
    out->labels.no_group = view_str(VS_MAP_NO_GROUP);
    out->labels.low = view_str(VS_TILE_LOW);

    out->spoolman.online = m->spoolman_online;
    for(i = 0; i < (int)m->unit_count && out->spoolman.pending_count < VIEW_MAX_PENDING_SPOOLS; i++) {
        int j;
        const mmu_unit_t *u = &m->units[i];

        for(j = 0; j < (int)u->slot_count && out->spoolman.pending_count < VIEW_MAX_PENDING_SPOOLS;
            j++) {
            if(u->slots[j].pending_confirmation) {
                char id[VIEW_ID_LEN];

                mmu_slot_id_in(m, i, j, id, sizeof(id));
                vs_copy(id, out->spoolman.pending[out->spoolman.pending_count], VIEW_ID_LEN);
                out->spoolman.pending_count++;
            }
        }
    }

    n = VIEW_MAX_SETTINGS;
    for(i = 0; i < (int)n; i++) {
        view_setting_t *s = &out->settings[i];
        const bool *field;
        bool value;
        char line_on[VIEW_ACTION_LINE_LEN], line_off[VIEW_ACTION_LINE_LEN];

        vs_copy(view_setting_key(i), s->key, sizeof(s->key));
        s->label = view_str(VS_SETTING_0_TITLE + 2 * i);
        s->note = view_str(VS_SETTING_0_NOTE + 2 * i);

        /* The last row is the language, not an on/off switch: a list of the
         * languages built in, each by its own name, with the current one
         * selected. It sends no action; the host calls core_set_language(). */
        if(i == VIEW_LANGUAGE_SETTING_ROW) {
            int j;

            s->is_choice = true;
            s->value = false;
            action_clear(&s->action_on);
            action_clear(&s->action_off);
            s->action_on.label = "";
            s->action_off.label = "";
            s->option_count = 0;
            s->selected = -1;
            for(j = 0; j < view_lang_count() && s->option_count < VIEW_MAX_LANGS; j++) {
                /* the pseudo-locale is for tests and layout review: it stays
                 * available through view_set_language(), never in the list */
                if(strcmp(view_lang_code(j), "qps") == 0) continue;
                if(j == view_lang_current()) s->selected = (int)s->option_count;
                s->options[s->option_count].value = (int32_t)j;
                s->options[s->option_count].label = view_lang_name(j);
                s->option_count++;
            }
            continue;
        }

        switch(i) {
        case 0: field = &m->settings.read_tag_on_insertion; break;
        case 1: field = &m->settings.read_tags_at_startup; break;
        case 2: field = &m->settings.auto_reload_on_runout; break;
        case 3: field = &m->settings.auto_create_spoolman_spools; break;
        default: field = &m->settings.apply_pa_on_load; break;
        }
        value = *field;

        s->value = value;

        snprintf(line_on, sizeof line_on, "setting %s on", view_setting_key(i));
        snprintf(line_off, sizeof line_off, "setting %s off", view_setting_key(i));

        action_clear(&s->action_on);
        vs_copy("setting_on", s->action_on.id, sizeof(s->action_on.id));
        s->action_on.label = view_str(VS_ACTION_ON);
        vs_copy(line_on, s->action_on.line, sizeof(s->action_on.line));
        s->action_on.style = VIEW_STYLE_NORMAL;
        action_check(&s->action_on, line_on, m);

        action_clear(&s->action_off);
        vs_copy("setting_off", s->action_off.id, sizeof(s->action_off.id));
        s->action_off.label = view_str(VS_ACTION_OFF);
        vs_copy(line_off, s->action_off.line, sizeof(s->action_off.line));
        s->action_off.style = VIEW_STYLE_NORMAL;
        action_check(&s->action_off, line_off, m);
    }
    out->setting_count = n;
}

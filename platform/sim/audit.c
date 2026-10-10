/**
 * @file audit.c
 * Layout audit for the simulator walk: finds content that does not fit.
 *
 * Reported per screen/modal state:
 *   CLIPPED   a child extends past a parent that neither scrolls in that
 *             direction nor shows overflow, so part of it is invisible
 *   SCROLLS   a child extends past a parent that scrolls (reachable, but the
 *             user has to know to scroll) - informational
 *   OVERLAP   two visible siblings partially cover each other
 *   TRUNCATED a label's text is wider than the label (clip/dots/scroll modes)
 *             or taller than it (wrap mode with a fixed height)
 */
#include "audit.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *where;
    int issues;
    int notes;
} audit_ctx_t;

static bool area_contains(const lv_area_t *outer, const lv_area_t *inner)
{
    return inner->x1 >= outer->x1 && inner->y1 >= outer->y1 && inner->x2 <= outer->x2 &&
           inner->y2 <= outer->y2;
}

static bool area_overlap(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

/* Tolerances: text boxes routinely share a row or two with the next line
 * (line height vs glyph height), which is invisible. */
#define OVERLAP_MIN_PX 3
#define CLIP_MIN_PX    2

static int32_t overlap_depth(const lv_area_t *a, const lv_area_t *b)
{
    int32_t w = LV_MIN(a->x2, b->x2) - LV_MAX(a->x1, b->x1) + 1;
    int32_t h = LV_MIN(a->y2, b->y2) - LV_MAX(a->y1, b->y1) + 1;
    return LV_MIN(w, h);
}

static int32_t overshoot(const lv_area_t *outer, const lv_area_t *inner)
{
    int32_t o = 0;
    o = LV_MAX(o, outer->x1 - inner->x1);
    o = LV_MAX(o, outer->y1 - inner->y1);
    o = LV_MAX(o, inner->x2 - outer->x2);
    o = LV_MAX(o, inner->y2 - outer->y2);
    return o;
}

static bool area_empty(const lv_area_t *a)
{
    return a->x2 < a->x1 || a->y2 < a->y1;
}

static const char *first_label_text(lv_obj_t *obj, int depth)
{
    uint32_t i, n;

    if(lv_obj_check_type(obj, &lv_label_class)) return lv_label_get_text(obj);
    if(depth <= 0) return NULL;
    n = lv_obj_get_child_count(obj);
    for(i = 0; i < n; i++) {
        const char *t = first_label_text(lv_obj_get_child(obj, (int32_t)i), depth - 1);
        if(t && t[0]) return t;
    }
    return NULL;
}

static void describe(lv_obj_t *obj, char *buf, size_t len)
{
    const char *kind = lv_obj_check_type(obj, &lv_label_class) ? "label" : "obj";
    const char *text = first_label_text(obj, 3);
    char snip[40];
    size_t i, j = 0;

    if(!text) text = "";
    /* one line, printable, short */
    for(i = 0; text[i] && j < sizeof(snip) - 4; i++) {
        unsigned char c = (unsigned char)text[i];
        if(c == '\n') c = ' ';
        if(c < 0x20) continue;
        if(c >= 0x80) continue; /* symbols from the icon font */
        snip[j++] = (char)c;
    }
    if(text[i]) {
        snip[j++] = '.';
        snip[j++] = '.';
    }
    snip[j] = '\0';
    snprintf(buf, len, "%s'%s'", kind, snip);
}

static void report(audit_ctx_t *ctx, bool issue, const char *kind, lv_obj_t *obj,
                   const lv_area_t *a, const char *extra)
{
    char desc[64];

    describe(obj, desc, sizeof(desc));
    printf("AUDIT %s %s %s [%d,%d..%d,%d] %s\n", ctx->where, kind, desc, (int)a->x1, (int)a->y1,
           (int)a->x2, (int)a->y2, extra ? extra : "");
    if(issue)
        ctx->issues++;
    else
        ctx->notes++;
}

static void check_label(audit_ctx_t *ctx, lv_obj_t *label)
{
    lv_area_t content;
    lv_point_t size;
    const char *text = lv_label_get_text(label);
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    int32_t letter = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
    int32_t line = lv_obj_get_style_text_line_space(label, LV_PART_MAIN);
    lv_label_long_mode_t mode = lv_label_get_long_mode(label);
    int32_t cw, ch;
    char extra[64];

    if(!text || !text[0] || !font) return;
    lv_obj_get_content_coords(label, &content);
    cw = content.x2 - content.x1 + 1;
    ch = content.y2 - content.y1 + 1;
    if(cw <= 0 || ch <= 0) return;

    if(mode == LV_LABEL_LONG_MODE_WRAP) {
        lv_text_get_size(&size, text, font, letter, line, cw, LV_TEXT_FLAG_NONE);
        if(size.y > ch + CLIP_MIN_PX) {
            snprintf(extra, sizeof(extra), "text %dpx tall in %dpx", (int)size.y, (int)ch);
            report(ctx, true, "TRUNCATED", label, &content, extra);
        }
    }
    else {
        lv_text_get_size(&size, text, font, letter, line, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if(size.x > cw + CLIP_MIN_PX) {
            snprintf(extra, sizeof(extra), "text %dpx wide in %dpx%s", (int)size.x, (int)cw,
                     mode == LV_LABEL_LONG_MODE_DOTS ? " (dots)" : "");
            report(ctx, true, "TRUNCATED", label, &content, extra);
        }
    }
}

static void walk(audit_ctx_t *ctx, lv_obj_t *parent)
{
    uint32_t i, j, n = lv_obj_get_child_count(parent);
    lv_area_t pa;

    lv_obj_get_coords(parent, &pa);

    for(i = 0; i < n; i++) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i);
        lv_area_t ca;

        if(lv_obj_is_hidden(child)) continue;
        lv_obj_get_coords(child, &ca);
        if(area_empty(&ca)) continue;

        if(overshoot(&pa, &ca) >= CLIP_MIN_PX && !lv_obj_is_overflow_visible(parent)) {
            lv_dir_t dir = lv_obj_get_scroll_dir(parent);
            bool scroll = lv_obj_is_scrollable(parent);
            bool out_v = ca.y1 < pa.y1 || ca.y2 > pa.y2;
            bool out_h = ca.x1 < pa.x1 || ca.x2 > pa.x2;
            bool reachable = scroll && (!out_v || (dir & LV_DIR_VER)) &&
                             (!out_h || (dir & LV_DIR_HOR));
            bool is_screen = lv_obj_get_parent(parent) == NULL;
            char extra[160];
            char pdesc[64];

            describe(parent, pdesc, sizeof(pdesc));
            snprintf(extra, sizeof(extra), "past parent %s [%d,%d..%d,%d]", pdesc, (int)pa.x1,
                     (int)pa.y1, (int)pa.x2, (int)pa.y2);
            /* A whole screen that scrolls takes the top bar with it: an issue. */
            if(reachable && !is_screen)
                report(ctx, false, "SCROLLS", child, &ca, extra);
            else
                report(ctx, true, "CLIPPED", child, &ca, extra);
        }

        if(lv_obj_check_type(child, &lv_label_class)) check_label(ctx, child);

        for(j = i + 1; j < n; j++) {
            lv_obj_t *other = lv_obj_get_child(parent, (int32_t)j);
            lv_area_t oa;
            char extra[160];
            char odesc[64];

            if(lv_obj_is_hidden(other)) continue;
            lv_obj_get_coords(other, &oa);
            if(area_empty(&oa) || !area_overlap(&ca, &oa)) continue;
            if(overlap_depth(&ca, &oa) < OVERLAP_MIN_PX) continue;
            if(area_contains(&ca, &oa) || area_contains(&oa, &ca)) continue;
            describe(other, odesc, sizeof(odesc));
            snprintf(extra, sizeof(extra), "overlaps %s [%d,%d..%d,%d]", odesc, (int)oa.x1,
                     (int)oa.y1, (int)oa.x2, (int)oa.y2);
            report(ctx, true, "OVERLAP", child, &ca, extra);
        }

        walk(ctx, child);
    }
}

int audit_run(const char *where)
{
    audit_ctx_t ctx = { where, 0, 0 };

    lv_obj_update_layout(lv_screen_active());
    walk(&ctx, lv_screen_active());
    walk(&ctx, lv_layer_top());
    return ctx.issues;
}

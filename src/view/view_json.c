/**
 * @file view_json.c
 * See view_json.h.
 */

#include "view/view_json.h"

#include "view/strings.h"
#include "view/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ enums */

static const char *tile_state_str(view_tile_state_t s)
{
    switch(s) {
    case VIEW_TILE_LOADED:      return "loaded";
    case VIEW_TILE_READY:       return "ready";
    case VIEW_TILE_EMPTY:       return "empty";
    case VIEW_TILE_POSITIONING: return "positioning";
    case VIEW_TILE_LOADING:     return "loading";
    case VIEW_TILE_UNLOADING:   return "unloading";
    case VIEW_TILE_ERROR:       return "error";
    case VIEW_TILE_RUNOUT:      return "runout";
    case VIEW_TILE_UNKNOWN:
    default:                    return "unknown";
    }
}

static const char *ink_str(view_ink_t i) { return (i == VIEW_INK_DARK) ? "dark" : "light"; }

static const char *activity_str(view_activity_kind_t k)
{
    switch(k) {
    case VIEW_ACTIVITY_LOADING:   return "loading";
    case VIEW_ACTIVITY_UNLOADING: return "unloading";
    case VIEW_ACTIVITY_RUNOUT:    return "runout";
    case VIEW_ACTIVITY_ERROR:     return "error";
    case VIEW_ACTIVITY_IDLE:
    default:                      return "idle";
    }
}

static const char *target_str(view_target_t t)
{
    switch(t) {
    case VIEW_TARGET_RING: return "ring";
    case VIEW_TARGET_TAG:  return "tag";
    case VIEW_TARGET_TILE:
    default:               return "tile";
    }
}

static const char *dryer_tone_str(view_dryer_tone_t t)
{
    switch(t) {
    case VIEW_DRYER_HEAT: return "heat";
    case VIEW_DRYER_HOLD: return "hold";
    case VIEW_DRYER_COOL: return "cool";
    case VIEW_DRYER_FAULT: return "fault";
    case VIEW_DRYER_OFF:
    default:               return "off";
    }
}

static const char *tone_str(view_tone_t t)
{
    switch(t) {
    case VIEW_TONE_ERROR: return "error";
    case VIEW_TONE_INFO:  return "info";
    case VIEW_TONE_NEUTRAL:
    default:              return "neutral";
    }
}

static const char *action_style_str(view_action_style_t s)
{
    switch(s) {
    case VIEW_STYLE_PRIMARY: return "primary";
    case VIEW_STYLE_DANGER:  return "danger";
    case VIEW_STYLE_NORMAL:
    default:                 return "normal";
    }
}

static const char *field_kind_str(view_field_kind_t k)
{
    switch(k) {
    case VIEW_FIELD_SELECT: return "select";
    case VIEW_FIELD_TOGGLE: return "toggle";
    case VIEW_FIELD_NUMBER:
    default:                return "number";
    }
}

static const char *severity_str(mmu_severity_t s)
{
    switch(s) {
    case MMU_SEV_STOP:  return "stop";
    case MMU_SEV_PAUSE: return "pause";
    case MMU_SEV_INFO:
    default:            return "info";
    }
}

static void hex_of(uint32_t rgb, char *buf, size_t n)
{
    snprintf(buf, n, "#%06x", (unsigned)(rgb & 0xFFFFFFu));
}

static cJSON *hex_json(uint32_t rgb)
{
    char buf[8];

    hex_of(rgb, buf, sizeof buf);
    return cJSON_CreateString(buf);
}

/* -------------------------------------------------------------- sub-shapes */

/* A fixed-string field is a pointer into a const table (view.h); one a builder
 * left unset is NULL, and the JSON says "" rather than nothing at all. */
static const char *text_of(const char *s)
{
    return s ? s : "";
}

static cJSON *json_confirm(const view_confirm_t *c)
{
    cJSON *o;

    if(!c->present) return cJSON_CreateNull();
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title", text_of(c->title));
    cJSON_AddStringToObject(o, "text", text_of(c->text));
    cJSON_AddStringToObject(o, "ok_label", text_of(c->ok_label));
    return o;
}

static cJSON *json_field(const view_field_t *f, const view_option_t *pool)
{
    cJSON *o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "id", f->id);
    cJSON_AddStringToObject(o, "label", text_of(f->label));
    cJSON_AddStringToObject(o, "kind", field_kind_str(f->kind));
    cJSON_AddNumberToObject(o, "value", (double)f->value);
    cJSON_AddNumberToObject(o, "min", (double)f->min);
    cJSON_AddNumberToObject(o, "max", (double)f->max);
    cJSON_AddNumberToObject(o, "step", (double)f->step);
    cJSON_AddStringToObject(o, "unit", text_of(f->unit));
    cJSON_AddBoolToObject(o, "required", f->required);
    if(f->kind == VIEW_FIELD_SELECT) {
        cJSON *opts = cJSON_CreateArray();
        int i;

        for(i = 0; i < (int)f->option_count; i++) {
            const view_option_t *op = &pool[f->option_first + i];
            cJSON *e = cJSON_CreateObject();

            cJSON_AddNumberToObject(e, "value", (double)op->value);
            cJSON_AddStringToObject(e, "label", op->label);
            if(op->has_color_field)
                cJSON_AddItemToObject(e, "color", op->color_known ? hex_json(op->color) : cJSON_CreateNull());
            cJSON_AddItemToArray(opts, e);
        }
        cJSON_AddItemToObject(o, "options", opts);
    }
    return o;
}

static cJSON *json_form(const view_form_t *f, const view_field_t *fields, const view_option_t *pool)
{
    cJSON *o, *list;
    int i;

    if(!f->present) return cJSON_CreateNull();
    o = cJSON_CreateObject();
    list = cJSON_CreateArray();
    for(i = 0; i < (int)f->field_count; i++)
        cJSON_AddItemToArray(list, json_field(&fields[f->field_first + i], pool));
    cJSON_AddItemToObject(o, "fields", list);
    /* Only the forms that own a submit button carry these; the others send
     * null, and a renderer treats that as "behave as before". */
    if(f->submit_label) cJSON_AddStringToObject(o, "submit_label", f->submit_label);
    else cJSON_AddNullToObject(o, "submit_label");
    cJSON_AddBoolToObject(o, "require_change", f->require_change);
    return o;
}

static cJSON *json_action(const view_action_t *a, const view_field_t *fields,
                          const view_option_t *pool)
{
    cJSON *o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "id", a->id);
    cJSON_AddStringToObject(o, "label", text_of(a->label));
    cJSON_AddStringToObject(o, "line", a->line);
    cJSON_AddBoolToObject(o, "enabled", a->enabled);
    cJSON_AddStringToObject(o, "reason", a->reason);
    cJSON_AddStringToObject(o, "style", action_style_str(a->style));
    cJSON_AddStringToObject(o, "target", target_str(a->target));
    cJSON_AddItemToObject(o, "confirm", json_confirm(&a->confirm));
    cJSON_AddItemToObject(o, "form", json_form(&a->form, fields, pool));
    return o;
}

static cJSON *json_actions(const view_actions_t *acts)
{
    cJSON *a = cJSON_CreateArray();
    int i;

    for(i = 0; i < (int)acts->count; i++)
        cJSON_AddItemToArray(a, json_action(&acts->items[i], acts->fields, acts->options));
    return a;
}

/* Each on-demand action list gets a function (a stack frame) of its own, so
 * the multi-KB view_actions_t is gone before the caller recurses. */
static cJSON *json_tile_actions(const mmu_model_t *m, int unit_idx, int bay)
{
    view_actions_t acts;

    if(!view_tile_actions(m, unit_idx, bay, &acts)) return cJSON_CreateArray();
    return json_actions(&acts);
}

static cJSON *json_unit_actions(const mmu_model_t *m, int unit_idx)
{
    view_actions_t acts;

    if(!view_unit_actions(m, unit_idx, &acts)) return cJSON_CreateArray();
    return json_actions(&acts);
}

static cJSON *json_toolhead_actions(const mmu_model_t *m, int th_idx)
{
    view_actions_t acts;

    if(!view_toolhead_actions(m, th_idx, &acts)) return cJSON_CreateArray();
    return json_actions(&acts);
}

static cJSON *json_alert_group(bool present, int severity, int count, const mmu_model_t *m,
                               int unit_idx, int th_idx);

static cJSON *json_tile(const mmu_model_t *m, int unit_idx, const view_tile_t *t)
{
    cJSON *o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "slot_id", t->slot_id);
    cJSON_AddNumberToObject(o, "bay", t->bay);
    cJSON_AddItemToObject(o, "tool", t->has_tool ? cJSON_CreateString(t->tool) : cJSON_CreateNull());
    cJSON_AddStringToObject(o, "label", t->label);
    cJSON_AddItemToObject(o, "sublabel",
                          t->has_sublabel ? cJSON_CreateString(t->sublabel) : cJSON_CreateNull());

    if(!t->spare.present) {
        cJSON_AddItemToObject(o, "spare", cJSON_CreateNull());
    }
    else {
        cJSON *sp = cJSON_CreateObject();

        cJSON_AddItemToObject(sp, "position",
                              t->spare.bare ? cJSON_CreateNull() : cJSON_CreateNumber(t->spare.position));
        cJSON_AddNumberToObject(sp, "count", t->spare.count);
        cJSON_AddItemToObject(o, "spare", sp);
    }

    cJSON_AddStringToObject(o, "state", tile_state_str(t->state));
    if(!t->has_tag) cJSON_AddItemToObject(o, "tag", cJSON_CreateNull());
    else {
        cJSON *tg = cJSON_CreateObject();

        cJSON_AddStringToObject(tg, "text", text_of(t->tag));
        cJSON_AddStringToObject(tg, "tone", tone_str(t->tag_tone));
        cJSON_AddStringToObject(tg, "detail", t->tag_detail);
        cJSON_AddStringToObject(tg, "code", t->tag_code);
        cJSON_AddItemToObject(o, "tag", tg);
    }

    cJSON_AddItemToObject(o, "color", t->color_known ? hex_json(t->color) : cJSON_CreateNull());
    cJSON_AddStringToObject(o, "ink", ink_str(t->ink));
    cJSON_AddItemToObject(o, "gradient_top",
                          t->color_known ? hex_json(t->gradient_top) : cJSON_CreateNull());
    cJSON_AddItemToObject(o, "gradient_bottom",
                          t->color_known ? hex_json(t->gradient_bottom) : cJSON_CreateNull());
    cJSON_AddStringToObject(o, "color_name", text_of(t->color_name));
    cJSON_AddBoolToObject(o, "dim", t->dim);
    cJSON_AddBoolToObject(o, "hatched", t->hatched);

    cJSON_AddStringToObject(o, "name", t->name);
    cJSON_AddStringToObject(o, "material", t->material);
    cJSON_AddStringToObject(o, "brand", t->brand);
    cJSON_AddStringToObject(o, "grams_text", t->grams_text);
    cJSON_AddNumberToObject(o, "pct", t->pct);
    cJSON_AddStringToObject(o, "pct_text", t->pct_text);
    cJSON_AddBoolToObject(o, "low", t->low);

    cJSON_AddBoolToObject(o, "rfid", t->rfid);
    cJSON_AddBoolToObject(o, "pending_confirmation", t->pending_confirmation);
    cJSON_AddBoolToObject(o, "calibrated", t->calibrated);

    cJSON_AddItemToObject(o, "actions", json_tile_actions(m, unit_idx, t->bay));
    return o;
}

/* Each on-demand action list gets a function (a stack frame) of its own, so
 * the multi-KB view_actions_t is gone before the caller recurses. */
static cJSON *json_group_actions(const mmu_model_t *m, int unit_idx, int index)
{
    view_actions_t acts;

    if(!view_group_actions(m, unit_idx, index, &acts)) return cJSON_CreateArray();
    return json_actions(&acts);
}

static cJSON *json_group_member(const view_group_member_t *mb)
{
    cJSON *o = cJSON_CreateObject();

    cJSON_AddStringToObject(o, "slot_id", mb->slot_id);
    cJSON_AddNumberToObject(o, "bay", mb->bay);
    cJSON_AddBoolToObject(o, "mine", mb->mine);
    cJSON_AddBoolToObject(o, "dim", mb->dim);
    cJSON_AddBoolToObject(o, "spare", mb->spare);
    cJSON_AddStringToObject(o, "label", mb->label);
    cJSON_AddItemToObject(o, "color", mb->color_known ? hex_json(mb->color) : cJSON_CreateNull());
    cJSON_AddStringToObject(o, "material", mb->material);
    cJSON_AddStringToObject(o, "brand", mb->brand);
    cJSON_AddStringToObject(o, "state", text_of(mb->state));
    cJSON_AddStringToObject(o, "state_tone", tone_str(mb->state_tone));
    return o;
}

static void json_group_body(cJSON *o, const mmu_model_t *m, int unit_idx, int index)
{
    view_group_t vg;
    cJSON *members;
    int i;

    if(!view_group(m, unit_idx, index, &vg)) return;
    cJSON_AddStringToObject(o, "name", vg.name);
    cJSON_AddBoolToObject(o, "editable", vg.editable);
    cJSON_AddStringToObject(o, "reason", text_of(vg.reason));
    members = cJSON_CreateArray();
    for(i = 0; i < (int)vg.member_count; i++)
        cJSON_AddItemToArray(members, json_group_member(&vg.members[i]));
    cJSON_AddItemToObject(o, "members", members);
}

/* The groups list of one unit. The body's view_group_t and the delete action's
 * view_actions_t are built by sibling calls on purpose: each frame is gone
 * before the next one starts, so the two never share a stack. */
static cJSON *json_group(const mmu_model_t *m, int unit_idx, int index)
{
    cJSON *o = cJSON_CreateObject();

    cJSON_AddItemToObject(o, "actions", json_group_actions(m, unit_idx, index));
    json_group_body(o, m, unit_idx, index);
    return o;
}

static cJSON *json_unit(const mmu_model_t *m, int unit_idx)
{
    view_unit_t vu;
    cJSON *o, *bays;
    int i;

    if(!view_unit(m, unit_idx, &vu)) return cJSON_CreateNull();

    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", vu.subtitle);
    cJSON_AddStringToObject(o, "title", vu.title);
    cJSON_AddStringToObject(o, "subtitle", vu.subtitle);
    cJSON_AddBoolToObject(o, "online", vu.online);
    cJSON_AddStringToObject(o, "status_text", vu.status_text);

    if(!vu.env.present) cJSON_AddItemToObject(o, "env", cJSON_CreateNull());
    else {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "text", vu.env.text);
        cJSON_AddItemToObject(o, "env", e);
    }

    if(!vu.dryer.present) cJSON_AddItemToObject(o, "dryer", cJSON_CreateNull());
    else {
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "text", vu.dryer.text);
        cJSON_AddStringToObject(d, "tone", dryer_tone_str(vu.dryer.tone));
        cJSON_AddBoolToObject(d, "adapter_missing", vu.dryer.adapter_missing);
        cJSON_AddItemToObject(o, "dryer", d);
    }

    cJSON_AddItemToObject(o, "alert", json_alert_group(vu.alert.present, vu.alert.severity, vu.alert.count, m, unit_idx, -1));
    cJSON_AddBoolToObject(o, "alert_clear", vu.alert.clear);

    if(!vu.info.present) cJSON_AddItemToObject(o, "info", cJSON_CreateNull());
    else {
        cJSON *inf = cJSON_CreateObject();
        cJSON_AddStringToObject(inf, "serial", "");
        cJSON_AddStringToObject(inf, "firmware", "");
        cJSON_AddStringToObject(inf, "family", vu.info.family);
        cJSON_AddItemToObject(o, "info", inf);
    }

    cJSON_AddItemToObject(o, "actions", json_unit_actions(m, unit_idx));

    /* The groups list, scoped to this unit: every group that holds one of its
     * bays, in order, and then the bays that hold none. */
    {
        cJSON *groups = cJSON_CreateArray();
        int n = view_group_count(m, unit_idx);

        for(i = 0; i < n; i++) cJSON_AddItemToArray(groups, json_group(m, unit_idx, i));
        cJSON_AddItemToObject(o, "groups", groups);
    }
    {
        view_ungrouped_t ug;
        cJSON *free_bays = cJSON_CreateArray();

        if(view_ungrouped_bays(m, unit_idx, &ug)) {
            for(i = 0; i < (int)ug.count; i++) cJSON_AddItemToArray(free_bays, json_group_member(&ug.bays[i]));
        }
        cJSON_AddItemToObject(o, "ungrouped_bays", free_bays);
    }

    bays = cJSON_CreateArray();
    for(i = 0; i < (int)vu.bay_count; i++) {
        view_tile_t t;

        if(view_tile(m, unit_idx, i, &t)) cJSON_AddItemToArray(bays, json_tile(m, unit_idx, &t));
    }
    cJSON_AddItemToObject(o, "bays", bays);
    return o;
}

static cJSON *json_toolhead(const mmu_model_t *m, int th_idx)
{
    view_toolhead_t vt;
    cJSON *o, *units;
    int i;

    if(!view_toolhead(m, th_idx, &vt)) return cJSON_CreateNull();

    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", vt.id);
    cJSON_AddStringToObject(o, "title", vt.title);
    cJSON_AddStringToObject(o, "subtitle", vt.subtitle);

    if(!vt.tool.present) cJSON_AddItemToObject(o, "tool", cJSON_CreateNull());
    else {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "label", vt.tool.label);
        cJSON_AddItemToObject(t, "color",
                              vt.tool.color_known ? hex_json(vt.tool.color) : cJSON_CreateNull());
        cJSON_AddStringToObject(t, "ink", ink_str(vt.tool.ink));
        cJSON_AddBoolToObject(t, "outline", vt.tool.outline);
        cJSON_AddItemToObject(o, "tool", t);
    }

    if(!vt.pressure.present) cJSON_AddItemToObject(o, "pressure", cJSON_CreateNull());
    else {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "value", (double)vt.pressure.value);
        cJSON_AddNumberToObject(p, "set_point", (double)vt.pressure.set_point);
        if(!vt.pressure.has_band) cJSON_AddItemToObject(p, "band", cJSON_CreateNull());
        else {
            cJSON *band = cJSON_CreateArray();
            cJSON_AddItemToArray(band, cJSON_CreateNumber((double)vt.pressure.band_low));
            cJSON_AddItemToArray(band, cJSON_CreateNumber((double)vt.pressure.band_high));
            cJSON_AddItemToObject(p, "band", band);
        }
        cJSON_AddStringToObject(p, "label", text_of(vt.pressure.label));
        cJSON_AddStringToObject(p, "text", vt.pressure.text);
        {
            cJSON *scale = cJSON_CreateArray();
            int k;

            for(k = 0; k < VIEW_SCALE_COUNT; k++)
                cJSON_AddItemToArray(scale, cJSON_CreateString(vt.pressure.scale[k]));
            cJSON_AddItemToObject(p, "scale", scale);
        }
        cJSON_AddItemToObject(o, "pressure", p);
    }

    {
        cJSON *act = cJSON_CreateObject();
        cJSON *steps = cJSON_CreateArray();
        int i2;

        cJSON_AddStringToObject(act, "kind", activity_str(vt.activity.kind));
        for(i2 = 0; i2 < (int)vt.activity.step_count; i2++)
            cJSON_AddItemToArray(steps, cJSON_CreateString(vt.activity.steps[i2]));
        cJSON_AddItemToObject(act, "steps", steps);
        cJSON_AddNumberToObject(act, "index", vt.activity.index);
        cJSON_AddNumberToObject(act, "failed", vt.activity.failed);
        if(!vt.activity.rest.present) cJSON_AddItemToObject(act, "rest", cJSON_CreateNull());
        else {
            cJSON *rest = cJSON_CreateObject();

            cJSON_AddStringToObject(rest, "label", vt.activity.rest.label);
            cJSON_AddItemToObject(rest, "color",
                                  vt.activity.rest.color_known ? hex_json(vt.activity.rest.color) : cJSON_CreateNull());
            cJSON_AddBoolToObject(rest, "loaded", vt.activity.rest.loaded);
            cJSON_AddBoolToObject(rest, "outline", vt.activity.rest.outline);
            cJSON_AddItemToObject(act, "rest", rest);
        }
        cJSON_AddItemToObject(o, "activity", act);
    }

    {
        cJSON *msg = cJSON_CreateObject();

        cJSON_AddStringToObject(msg, "text", vt.message.text);
        cJSON_AddStringToObject(msg, "tone", tone_str(vt.message.tone));
        cJSON_AddItemToObject(o, "message", msg);
    }
    cJSON_AddItemToObject(o, "alert", json_alert_group(vt.alert.present, vt.alert.severity, vt.alert.count, m, -1, th_idx));
    cJSON_AddBoolToObject(o, "alert_clear", vt.alert.clear);

    cJSON_AddItemToObject(o, "actions", json_toolhead_actions(m, th_idx));

    units = cJSON_CreateArray();
    for(i = 0; i < (int)m->unit_count; i++) {
        if(m->units[i].toolhead == th_idx) cJSON_AddItemToArray(units, json_unit(m, i));
    }
    cJSON_AddItemToObject(o, "units", units);
    return o;
}

static cJSON *json_alert_item(const view_alert_t *va)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *acts = cJSON_CreateArray();
    int i;

    cJSON_AddStringToObject(o, "severity", severity_str(va->severity));
    cJSON_AddStringToObject(o, "code", va->code);
    cJSON_AddStringToObject(o, "title", text_of(va->title));
    cJSON_AddStringToObject(o, "text", va->text);
    cJSON_AddItemToObject(o, "unit", va->has_unit ? cJSON_CreateString(va->unit) : cJSON_CreateNull());
    cJSON_AddItemToObject(o, "slot_id",
                          va->has_slot ? cJSON_CreateString(va->slot_id) : cJSON_CreateNull());
    cJSON_AddStringToObject(o, "when_text", va->when_text);
    cJSON_AddBoolToObject(o, "unread", va->unread);
    for(i = 0; i < (int)va->action_count; i++) cJSON_AddItemToArray(acts, json_action(&va->actions[i], NULL, NULL));
    cJSON_AddItemToObject(o, "actions", acts);
    return o;
}

static cJSON *json_alert(const mmu_model_t *m, int idx)
{
    view_alert_t va;

    if(!view_alert(m, idx, &va)) return cJSON_CreateNull();
    return json_alert_item(&va);
}

/* {severity, count, items} for a unit's (unit_idx >= 0) or a toolhead's alert
 * group; null when it has none. The items are built here, in a frame of their
 * own (their actions are large). */
static cJSON *json_alert_group(bool present, int severity, int count, const mmu_model_t *m,
                               int unit_idx, int th_idx)
{
    view_alerts_t g;
    cJSON *o, *items;
    int i;

    if(!present) return cJSON_CreateNull();
    if(unit_idx >= 0 ? !view_unit_alerts(m, unit_idx, &g) : !view_toolhead_alerts(m, th_idx, &g))
        return cJSON_CreateNull();
    o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "severity", severity_str((mmu_severity_t)severity));
    cJSON_AddNumberToObject(o, "count", count);
    cJSON_AddStringToObject(o, "tone", tone_str(g.tone));
    items = cJSON_CreateArray();
    for(i = 0; i < (int)g.count; i++) cJSON_AddItemToArray(items, json_alert_item(&g.items[i]));
    cJSON_AddItemToObject(o, "items", items);
    return o;
}

static cJSON *json_setting(const view_setting_t *s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *options = cJSON_CreateArray();
    int i;

    cJSON_AddStringToObject(o, "key", s->key);
    cJSON_AddStringToObject(o, "label", text_of(s->label));
    cJSON_AddStringToObject(o, "note", text_of(s->note));
    cJSON_AddBoolToObject(o, "value", s->value);
    /* A choice row (the language) picks from a list and sends no action; the
     * host calls setLanguage() with the code behind the option it chose. */
    if(s->is_choice) {
        cJSON_AddItemToObject(o, "action_on", cJSON_CreateNull());
        cJSON_AddItemToObject(o, "action_off", cJSON_CreateNull());
        for(i = 0; i < (int)s->option_count; i++) {
            cJSON *e = cJSON_CreateObject();

            cJSON_AddStringToObject(e, "code", view_lang_code(s->options[i].value));
            cJSON_AddStringToObject(e, "label", text_of(s->options[i].label));
            cJSON_AddItemToArray(options, e);
        }
        cJSON_AddNumberToObject(o, "selected", (double)s->selected);
    }
    else {
        cJSON_AddItemToObject(o, "action_on", json_action(&s->action_on, NULL, NULL));
        cJSON_AddItemToObject(o, "action_off", json_action(&s->action_off, NULL, NULL));
        cJSON_AddNumberToObject(o, "selected", 0);
    }
    cJSON_AddItemToObject(o, "options", options);
    return o;
}

/* -------------------------------------------------------------------- root */

cJSON *view_json_build(const mmu_model_t *m)
{
    view_panel_t vp;
    cJSON *root, *toolheads, *unassigned, *alerts, *settings, *sp;
    int i;

    root = cJSON_CreateObject();
    if(!m) return root;

    view_panel(m, &vp);

    toolheads = cJSON_CreateArray();
    for(i = 0; i < (int)vp.toolhead_count; i++)
        cJSON_AddItemToArray(toolheads, json_toolhead(m, i));
    cJSON_AddItemToObject(root, "toolheads", toolheads);

    unassigned = cJSON_CreateArray();
    for(i = 0; i < (int)vp.unassigned_count; i++)
        cJSON_AddItemToArray(unassigned, json_unit(m, vp.unassigned_units[i]));
    cJSON_AddItemToObject(root, "unassigned_units", unassigned);

    alerts = cJSON_CreateArray();
    for(i = 0; i < (int)vp.alert_count; i++) cJSON_AddItemToArray(alerts, json_alert(m, vp.alert_idx[i]));
    cJSON_AddItemToObject(root, "alerts", alerts);

    cJSON_AddNumberToObject(root, "alert_count", vp.alert_count);
    cJSON_AddNumberToObject(root, "unread_count", vp.unread_count);

    if(!vp.notice.present) cJSON_AddItemToObject(root, "notice", cJSON_CreateNull());
    else {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "text", text_of(vp.notice.text));
        cJSON_AddItemToObject(root, "notice", n);
    }

    if(!vp.nothing_to_load.present) cJSON_AddItemToObject(root, "nothing_to_load", cJSON_CreateNull());
    else {
        cJSON *n = cJSON_CreateObject();
        cJSON_AddStringToObject(n, "text", text_of(vp.nothing_to_load.text));
        cJSON_AddItemToObject(root, "nothing_to_load", n);
    }

    {
        cJSON *lb = cJSON_CreateObject();

        cJSON_AddStringToObject(lb, "alerts", text_of(vp.labels.alerts));
        cJSON_AddStringToObject(lb, "settings", text_of(vp.labels.settings));
        cJSON_AddStringToObject(lb, "fault", text_of(vp.labels.fault));
        cJSON_AddStringToObject(lb, "pressure", text_of(vp.labels.pressure));
        cJSON_AddStringToObject(lb, "no_alerts", text_of(vp.labels.no_alerts));
        cJSON_AddStringToObject(lb, "close", text_of(vp.labels.close));
        cJSON_AddStringToObject(lb, "cancel", text_of(vp.labels.cancel));
        cJSON_AddStringToObject(lb, "no_response", text_of(vp.labels.no_response));
        cJSON_AddStringToObject(lb, "edit_failed", text_of(vp.labels.edit_failed));
        cJSON_AddStringToObject(lb, "action_refused", text_of(vp.labels.action_refused));
        cJSON_AddStringToObject(lb, "groups", text_of(vp.labels.groups));
        cJSON_AddStringToObject(lb, "no_group", text_of(vp.labels.no_group));
        cJSON_AddStringToObject(lb, "low", text_of(vp.labels.low));
        cJSON_AddItemToObject(root, "labels", lb);
    }

    sp = cJSON_CreateObject();
    cJSON_AddBoolToObject(sp, "online", vp.spoolman.online);
    {
        cJSON *pending = cJSON_CreateArray();

        for(i = 0; i < (int)vp.spoolman.pending_count; i++)
            cJSON_AddItemToArray(pending, cJSON_CreateString(vp.spoolman.pending[i]));
        cJSON_AddItemToObject(sp, "pending", pending);
    }
    cJSON_AddItemToObject(root, "spoolman", sp);

    settings = cJSON_CreateArray();
    for(i = 0; i < (int)vp.setting_count; i++)
        cJSON_AddItemToArray(settings, json_setting(&vp.settings[i]));
    cJSON_AddItemToObject(root, "settings", settings);

    return root;
}

char *view_json(const mmu_model_t *m)
{
    cJSON *root = view_json_build(m);
    char *text = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);
    return text;
}

# LVGL 9.6 cheat sheet for this project

Everything a UI file here needs, with exact signatures from `lib/lvgl/include/lvgl/`. **Do not open LVGL's sources**: if a call is not listed here, look at how `src/ui/*.c` already does it. Style setters take a `selector` = part | state, e.g. `0` (main part, default state), `LV_STATE_PRESSED`, `LV_PART_INDICATOR`, `LV_PART_KNOB`, `LV_PART_INDICATOR | LV_STATE_CHECKED`.

## Idioms

```c
/* A plain box you style yourself (no theme styles). */
lv_obj_t *o = lv_obj_create(parent);
lv_obj_remove_style_all(o);
lv_obj_set_pos(o, x, y);  lv_obj_set_size(o, w, h);
lv_obj_set_scrollable(o, false);
lv_obj_set_style_bg_color(o, UI_COL_PANEL, 0);
lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
lv_obj_set_style_radius(o, 10, 0);

/* Vertical gradient as the object's own background. */
lv_obj_set_style_bg_color(o, top, 0);
lv_obj_set_style_bg_grad_color(o, bottom, 0);
lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);

/* Pressed look: same setter with the state as selector. */
lv_obj_set_style_bg_color(btn, UI_COL_PANEL_HI, LV_STATE_PRESSED);

/* Border that exists but is invisible (keeps the content box fixed). */
lv_obj_set_style_border_width(o, 2, 0);
lv_obj_set_style_border_opa(o, LV_OPA_TRANSP, 0);

/* A label. */
lv_obj_t *l = lv_label_create(parent);
lv_label_set_text(l, "A1");
lv_obj_set_style_text_font(l, UI_FONT_XL, 0);
lv_obj_set_style_text_color(l, UI_COL_TEXT, 0);
lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);  /* or _WRAP / _CLIP; needs a fixed width */
lv_obj_set_width(l, 57);
lv_obj_set_pos(l, 6, 2);

/* Click / long-press. */
lv_obj_add_event_cb(o, on_click, LV_EVENT_CLICKED, user_data);
static void on_click(lv_event_t *e) { void *u = lv_event_get_user_data(e); lv_obj_t *t = lv_event_get_current_target(e); }
lv_obj_set_clickable(o, false);

/* Flex column that scrolls vertically. */
lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_set_style_pad_row(col, 6, 0);
lv_obj_set_scroll_dir(col, LV_DIR_VER);
lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
/* children: lv_obj_set_width(child, LV_PCT(100)); lv_obj_set_height(child, LV_SIZE_CONTENT); lv_obj_set_flex_grow(child, 1); */

/* Arc used as a ring (no knob, not clickable). */
lv_obj_t *arc = lv_arc_create(parent);
lv_obj_set_size(arc, 32, 32);
lv_arc_set_rotation(arc, 270);
lv_arc_set_bg_angles(arc, 0, 360);
lv_arc_set_angles(arc, 0, sweep);                     /* indicator 0..sweep degrees */
lv_obj_set_style_arc_width(arc, 3, 0);                 /* track */
lv_obj_set_style_arc_color(arc, UI_COL_LINE, 0);
lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
lv_obj_set_style_arc_color(arc, UI_COL_WARN, LV_PART_INDICATOR);
lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
lv_obj_set_clickable(arc, false);

/* Animation bound to an object (LVGL deletes it with the object). */
static void spin_cb(void *var, int32_t v) { lv_arc_set_end_angle((lv_obj_t *)var, v); }
lv_anim_t a;
lv_anim_init(&a);
lv_anim_set_var(&a, arc);
lv_anim_set_exec_cb(&a, spin_cb);
lv_anim_set_values(&a, 0, 360);
lv_anim_set_duration(&a, 1100);
lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
lv_anim_set_path_cb(&a, lv_anim_path_linear);           /* or lv_anim_path_ease_out */
lv_anim_start(&a);
/* y slide: exec cb = (lv_anim_exec_xcb_t)lv_obj_set_y; opacity: a cb calling lv_obj_set_style_opa(var, v, 0) or bg_opa. */

/* Colors. */
lv_color_hex(0xD23B3B);  lv_color_mix(c1, c2, mix /* 255 = all c1 */);  lv_color_lighten(c, lvl);  lv_color_darken(c, lvl);
```

Constants that exist: `LV_ALIGN_TOP_LEFT LV_ALIGN_CENTER LV_ALIGN_LEFT_MID LV_ALIGN_RIGHT_MID LV_ALIGN_BOTTOM_MID LV_ALIGN_OUT_RIGHT_BOTTOM`, `LV_FLEX_FLOW_ROW LV_FLEX_FLOW_COLUMN`, `LV_FLEX_ALIGN_START/CENTER/END/SPACE_BETWEEN`, `LV_GRAD_DIR_VER`, `LV_BORDER_SIDE_TOP`, `LV_STATE_PRESSED/CHECKED/DISABLED`, `LV_PART_MAIN/INDICATOR/KNOB`, `LV_EVENT_CLICKED/LONG_PRESSED/DELETE`, `LV_OPA_TRANSP/COVER/70` (and 10..90 in tens), `LV_RADIUS_CIRCLE`, `LV_SIZE_CONTENT`, `LV_PCT(x)`, `LV_TEXT_ALIGN_LEFT/CENTER/RIGHT`, `LV_DIR_VER`, `LV_SCROLLBAR_MODE_OFF`, `LV_ANIM_REPEAT_INFINITE`, `LV_LABEL_LONG_MODE_WRAP/DOTS/CLIP`.

`lv_obj_add_flag` / `lv_obj_remove_flag` / `lv_obj_has_flag` are DEPRECATED in this LVGL: use `lv_obj_set_hidden`, `lv_obj_is_hidden`, `lv_obj_set_clickable`, `lv_obj_set_scrollable`.

## Signatures

```c
bool lv_anim_delete(void * var, lv_anim_exec_xcb_t exec_cb);
bool lv_obj_has_state(const lv_obj_t * obj, lv_state_t state);
bool lv_obj_is_hidden(const lv_obj_t * obj);
char * lv_label_get_text(const lv_obj_t * obj);
int32_t lv_anim_path_ease_in(const lv_anim_t * a);
int32_t lv_anim_path_ease_out(const lv_anim_t * a);
int32_t lv_anim_path_linear(const lv_anim_t * a);
int32_t lv_obj_get_height(const lv_obj_t * obj);
int32_t lv_obj_get_width(const lv_obj_t * obj);
int32_t lv_obj_get_x(const lv_obj_t * obj);
int32_t lv_obj_get_y(const lv_obj_t * obj);
lv_anim_t * lv_anim_start(const lv_anim_t * a);
lv_color_t LV_ATTRIBUTE_FAST_MEM lv_color_mix(lv_color_t c1, lv_color_t c2, uint8_t mix);
lv_color_t lv_color_darken(lv_color_t c, lv_opa_t lvl);
lv_color_t lv_color_hex(uint32_t c);
lv_color_t lv_color_lighten(lv_color_t c, lv_opa_t lvl);
lv_event_dsc_t * lv_obj_add_event_cb(lv_obj_t * obj, lv_event_cb_t event_cb, lv_event_code_t filter, void * user_data);
lv_obj_t * lv_arc_create(lv_obj_t * parent);
lv_obj_t * lv_button_create(lv_obj_t * parent);
lv_obj_t * lv_event_get_target_obj(lv_event_t * e);
lv_obj_t * lv_label_create(lv_obj_t * parent);
lv_obj_t * lv_layer_top(void);
lv_obj_t * lv_line_create(lv_obj_t * parent);
lv_obj_t * lv_obj_create(lv_obj_t * parent);
lv_obj_t * lv_obj_get_child(const lv_obj_t * obj, int32_t idx);
lv_obj_t * lv_screen_active(void);
lv_obj_t * parent = lv_obj_get_parent(obj);
lv_timer_t * lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period, void * user_data);
uint32_t lv_color_to_u32(lv_color_t color);
uint32_t lv_obj_get_child_count(const lv_obj_t * obj);
void lv_anim_init(lv_anim_t * a);
void lv_anim_set_completed_cb(lv_anim_t * a, lv_anim_completed_cb_t completed_cb);
void lv_anim_set_custom_exec_cb(lv_anim_t * a, lv_anim_custom_exec_cb_t exec_cb);
void lv_anim_set_delay(lv_anim_t * a, uint32_t delay);
void lv_anim_set_duration(lv_anim_t * a, uint32_t duration);
void lv_anim_set_exec_cb(lv_anim_t * a, lv_anim_exec_xcb_t exec_cb);
void lv_anim_set_path_cb(lv_anim_t * a, lv_anim_path_cb_t path_cb);
void lv_anim_set_repeat_count(lv_anim_t * a, uint32_t cnt);
void lv_anim_set_values(lv_anim_t * a, int32_t start, int32_t end);
void lv_anim_set_var(lv_anim_t * a, void * var);
void lv_arc_set_angles(lv_obj_t * obj, lv_value_precise_t start, lv_value_precise_t end);
void lv_arc_set_bg_angles(lv_obj_t * obj, lv_value_precise_t start, lv_value_precise_t end);
void lv_arc_set_end_angle(lv_obj_t * obj, lv_value_precise_t end);
void lv_arc_set_mode(lv_obj_t * obj, lv_arc_mode_t type);
void lv_arc_set_range(lv_obj_t * obj, int32_t min, int32_t max);
void lv_arc_set_rotation(lv_obj_t * obj, int32_t rotation);
void lv_arc_set_start_angle(lv_obj_t * obj, lv_value_precise_t start);
void lv_arc_set_value(lv_obj_t * obj, int32_t value);
void * lv_event_get_current_target(lv_event_t * e);
void * lv_event_get_target(lv_event_t * e);
void * lv_event_get_user_data(lv_event_t * e);
void lv_label_set_long_mode(lv_obj_t * obj, lv_label_long_mode_t long_mode);
void lv_label_set_text_fmt(lv_obj_t * obj, const char * fmt, ...) LV_FORMAT_ATTRIBUTE(2, 3);
void lv_label_set_text(lv_obj_t * obj, const char * text);
void lv_line_set_points(lv_obj_t * obj, const lv_point_precise_t points[], uint32_t point_num);
void lv_obj_add_state(lv_obj_t * obj, lv_state_t state);
void lv_obj_add_style(lv_obj_t * obj, const lv_style_t * style, lv_style_selector_t selector);
void lv_obj_align(lv_obj_t * obj, lv_align_t align, int32_t x_ofs, int32_t y_ofs);
void lv_obj_align_to(lv_obj_t * obj, const lv_obj_t * base, lv_align_t align, int32_t x_ofs, int32_t y_ofs);
void lv_obj_center(lv_obj_t * obj);
void lv_obj_clean(lv_obj_t * obj);
void lv_obj_delete(lv_obj_t * obj);
void * lv_obj_get_user_data(lv_obj_t * obj);
void lv_obj_remove_state(lv_obj_t * obj, lv_state_t state);
void lv_obj_remove_style_all(lv_obj_t * obj);
void lv_obj_set_clickable(lv_obj_t * obj, bool en);
void lv_obj_set_flex_align(lv_obj_t * obj, lv_flex_align_t main_place, lv_flex_align_t cross_place, lv_flex_align_t track_cross_place);
void lv_obj_set_flex_flow(lv_obj_t * obj, lv_flex_flow_t flow);
void lv_obj_set_flex_grow(lv_obj_t * obj, uint8_t grow);
void lv_obj_set_height(lv_obj_t * obj, int32_t h);
void lv_obj_set_hidden(lv_obj_t * obj, bool en);
void lv_obj_set_pos(lv_obj_t * obj, int32_t x, int32_t y);
void lv_obj_set_scrollable(lv_obj_t * obj, bool en);
void lv_obj_set_scroll_dir(lv_obj_t * obj, lv_dir_t dir);
void lv_obj_set_size(lv_obj_t * obj, int32_t w, int32_t h);
void lv_obj_set_state(lv_obj_t * obj, lv_state_t state, bool v);
void lv_obj_set_style_arc_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_arc_opa(lv_obj_t * obj, lv_opa_t value, lv_style_selector_t selector);
void lv_obj_set_style_arc_rounded(lv_obj_t * obj, bool value, lv_style_selector_t selector);
void lv_obj_set_style_arc_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_bg_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_bg_grad_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_bg_grad_dir(lv_obj_t * obj, lv_grad_dir_t value, lv_style_selector_t selector);
void lv_obj_set_style_bg_image_src(lv_obj_t * obj, const void * value, lv_style_selector_t selector);
void lv_obj_set_style_bg_opa(lv_obj_t * obj, lv_opa_t value, lv_style_selector_t selector);
void lv_obj_set_style_border_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_border_opa(lv_obj_t * obj, lv_opa_t value, lv_style_selector_t selector);
void lv_obj_set_style_border_side(lv_obj_t * obj, lv_border_side_t value, lv_style_selector_t selector);
void lv_obj_set_style_border_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_clip_corner(lv_obj_t * obj, bool value, lv_style_selector_t selector);
void lv_obj_set_style_line_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_line_rounded(lv_obj_t * obj, bool value, lv_style_selector_t selector);
void lv_obj_set_style_line_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_max_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_min_height(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_min_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_opa(lv_obj_t * obj, lv_opa_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_all(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_bottom(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_column(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_hor(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_left(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_right(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_row(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_top(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_pad_ver(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_radius(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_shadow_width(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_style_text_align(lv_obj_t * obj, lv_text_align_t value, lv_style_selector_t selector);
void lv_obj_set_style_text_color(lv_obj_t * obj, lv_color_t value, lv_style_selector_t selector);
void lv_obj_set_style_text_font(lv_obj_t * obj, const lv_font_t * value, lv_style_selector_t selector);
void lv_obj_set_style_text_opa(lv_obj_t * obj, lv_opa_t value, lv_style_selector_t selector);
void lv_obj_set_style_transition(lv_obj_t * obj, const lv_style_transition_dsc_t * value, lv_style_selector_t selector);
void lv_obj_set_style_translate_y(lv_obj_t * obj, int32_t value, lv_style_selector_t selector);
void lv_obj_set_user_data(lv_obj_t * obj, void * user_data);
void lv_obj_set_width(lv_obj_t * obj, int32_t w);
void lv_obj_set_x(lv_obj_t * obj, int32_t x);
void lv_obj_set_y(lv_obj_t * obj, int32_t y);
void lv_obj_update_layout(const lv_obj_t * obj);
void lv_style_init(lv_style_t * style);
void lv_style_transition_dsc_init(lv_style_transition_dsc_t * tr, const lv_style_prop_t props[], lv_anim_path_cb_t path_cb, uint32_t time, uint32_t delay, void * user_data);
void lv_timer_delete(lv_timer_t * timer);
void lv_timer_set_repeat_count(lv_timer_t * timer, int32_t repeat_count);
void lv_obj_set_scrollbar_mode(lv_obj_t * obj, lv_scrollbar_mode_t mode);
```

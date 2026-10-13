/**
 * @file screen_tag_writer.c
 * The spool tag writer as one of the display's screens (menu -> Tag writer).
 * The screen is an empty object handed to the tag writer (platform/esp32/
 * tag_writer), which builds its own pages into it. Leaving goes home like any
 * other screen: the display deletes the object, and the tag writer stops its
 * timer and lets go of the reader. The screen is never rebuilt by the live
 * model (app_live.c), since its state is the tag writer's own.
 *
 * ESP32 image with MMU_SPOOL_TAG_WRITER only.
 */
#include "ui/ui_screens.h"

#include "tag_writer.h"
#include "net_link.h"

static void on_delete(lv_event_t *e);

static void leave_tag_writer(void)
{
    /* The link is resumed once, in on_delete (the pause is ref-counted, so
     * resuming here as well would drop a console command's own pause). The
     * display deletes this screen as it goes home, which fires on_delete. */
    ui_nav_goto_async(UI_SCREEN_HOME);
}

static lv_obj_t *build(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    if(!scr) return NULL;
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    /* Free the Moonraker link's heap first: the reader needs it, and this
     * screen hides the live panel while it is open. net_link_resume() runs
     * when the user leaves (and defensively on delete, below). */
    net_link_pause();
    tag_writer_open(scr, leave_tag_writer);
    lv_obj_add_event_cb(scr, on_delete, LV_EVENT_DELETE, NULL);
    return scr;
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    net_link_resume();
}

static int modal_count(void)
{
    return 0;
}

static void open_modal(int index)
{
    (void)index;
}

const ui_screen_ops_t screen_tag_writer_ops = { build, modal_count, open_modal, NULL };

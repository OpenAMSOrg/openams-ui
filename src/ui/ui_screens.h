/**
 * @file ui_screens.h
 * The screen contract. Each screen lives in src/ui/screen_<name>.c and exports
 * one ui_screen_ops_t. ui_nav builds screens through this table, and the
 * simulator's --smoke / --screenshots walk uses modal_count/open_modal to open
 * every modal a screen offers.
 */
#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "lvgl.h"
#include "ui/ui_nav.h"

typedef struct {
    /** Build a new screen object (lv_obj_create(NULL)) for the current model state. */
    lv_obj_t *(*build)(void);
    /** How many distinct modals this screen can open (0 if none). */
    int (*modal_count)(void);
    /** Open modal @p index (0..modal_count-1) on lv_layer_top(), with dummy context. */
    void (*open_modal)(int index);
    /** Bring the objects of the screen already on screen up to date with the
     * model, in place: no object is created or deleted, and nothing is moved.
     * Returns false - having changed nothing - when the screen's structure
     * would differ from a fresh build() (a different number of bays, a widget
     * that appears or disappears, another variant), so the caller rebuilds
     * instead. NULL (the default) means "always rebuild".
     *
     * The contract: after this returns true the screen is pixel-identical to a
     * build() of the same model. */
    bool (*update)(void);
} ui_screen_ops_t;

/** The ops for @p id; never NULL for a valid id. */
const ui_screen_ops_t *ui_screen_ops(ui_screen_id_t id);

/** Placeholder body shared by screens that are not built yet. */
lv_obj_t *ui_placeholder_screen(ui_screen_id_t id);

extern const ui_screen_ops_t screen_home_ops;
extern const ui_screen_ops_t screen_load_ops;
extern const ui_screen_ops_t screen_alerts_ops;
extern const ui_screen_ops_t screen_edit_ops;
extern const ui_screen_ops_t screen_environment_ops;
extern const ui_screen_ops_t screen_print_ops;
extern const ui_screen_ops_t screen_refill_ops;
extern const ui_screen_ops_t screen_settings_ops;
extern const ui_screen_ops_t screen_mapping_ops;
extern const ui_screen_ops_t screen_order_ops;
extern const ui_screen_ops_t screen_unitinfo_ops;
extern const ui_screen_ops_t screen_debug_ops;

/** Arm the next UI_SCREEN_EDIT build in its save-and-load mode: the material
 *  starts unset, the button reads "Save and load", and the load is sent once
 *  the host has taken the spool. Call before ui_nav_goto_async(UI_SCREEN_EDIT). */
void screen_edit_save_and_load(void);

#endif /* UI_SCREENS_H */

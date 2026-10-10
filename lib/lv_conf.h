/*
 * LVGL looks for lv_conf.h in the folder above its own (lib/). The one shared
 * configuration is the repository's root lv_conf.h; this file only includes
 * it. A plain file rather than a symbolic link, so that a checkout without
 * symbolic links (git on Windows, by default) builds too.
 */
#include "../lv_conf.h"

/**
 * @file actions.c
 */
#include "app/actions.h"

#include <stdarg.h>
#include <stdio.h>

void actions_log(const char *fmt, ...)
{
    char line[192];   /* a view action line (VIEW_ACTION_LINE_LEN) fits whole */
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    platform_action(line);
}

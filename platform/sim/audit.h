/**
 * @file audit.h
 * Simulator-only layout audit (see audit.c).
 */
#ifndef AUDIT_H
#define AUDIT_H

#include "lvgl.h"

/** Audit the active screen and the top layer; prints one AUDIT line per
 * finding tagged @p where, returns the number of issues (notes excluded). */
int audit_run(const char *where);

#endif /* AUDIT_H */

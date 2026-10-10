/**
 * @file actions.h
 * Every user action goes through here. In this phase actions only log a
 * readable line (and screens may animate the dummy model); later the same
 * calls drive the printer.
 */
#ifndef ACTIONS_H
#define ACTIONS_H

/** Log one action, printf-style, e.g. actions_log("load %s", "A2"). */
void actions_log(const char *fmt, ...);

/** Implemented by the platform: deliver one formatted action line. */
void platform_action(const char *line);

#endif /* ACTIONS_H */

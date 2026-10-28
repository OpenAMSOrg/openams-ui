/**
 * @file backlight.h
 * The panel backlight as an LEDC PWM output, shared by both boards' display
 * drivers (main/board.h); display_set_brightness() (display.h) drives it.
 */
#ifndef BACKLIGHT_H
#define BACKLIGHT_H

/* Sets up the PWM on @p gpio at duty 0: the backlight stays off until
 * display_set_brightness() is called. */
void backlight_init(int gpio);

#endif /* BACKLIGHT_H */

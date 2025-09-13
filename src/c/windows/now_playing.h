#ifndef NOW_PLAYING_H
#define NOW_PLAYING_H

#include <pebble.h>
#include "../core/app_state.h"

// Now playing window functions
void now_playing_init(void);
void now_playing_deinit(void);
void now_playing_window_create(void);
void now_playing_window_pop(void);
void now_playing_window_destroy(void);
void now_playing_window_load(Window *window);
void now_playing_window_unload(Window *window);

// Click handlers
void now_playing_click_handler(ClickRecognizerRef recognizer, void *context);
void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *context);
void now_playing_click_config_provider(void *context);

// Display and control functions
void now_playing_update_display(void);
void now_playing_handle_action(ButtonId button);
void now_playing_update_clock(void);

// Volume control functions
void now_playing_request_volume_change(ButtonId button, int delta);
void now_playing_apply_volume_change(void);
void now_playing_show_volume_error(ButtonId button);
void now_playing_clear_volume_error(void);

// Utility functions
int now_playing_calculate_text_height(const char *text, GFont font, int width);

#endif // NOW_PLAYING_H

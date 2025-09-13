#ifndef AUTH_WINDOW_H
#define AUTH_WINDOW_H

#include <pebble.h>
#include "../core/app_state.h"

// Authentication window functions
void auth_window_init(void);
void auth_window_deinit(void);
void auth_window_load(Window *window);
void auth_window_unload(Window *window);
void auth_window_create(void);
void auth_window_destroy(void);

// Click handlers
void auth_click_handler(ClickRecognizerRef recognizer, void *context);
void auth_click_config_provider(void *context);

// Authentication functions
void auth_request_authentication(void);
void auth_handle_success(DictionaryIterator *iter);
void auth_handle_error(DictionaryIterator *iter);

#endif // AUTH_WINDOW_H

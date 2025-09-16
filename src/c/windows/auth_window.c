#include "auth_window.h"
#include "../api/spotify_api.h"
#include "../core/app_state.h"
#include "../core/constants.h"
#include "message_keys.auto.h"
#include "now_playing.h"

void auth_window_init(void) {
  // Authentication window will be created when needed
}

void auth_window_deinit(void) {
  auth_window_destroy();
}

void auth_window_create(void) {
  if (s_app_data.auth_window) {
    return;
  }
  
  s_app_data.auth_window = window_create();
  window_set_window_handlers(s_app_data.auth_window, (WindowHandlers) {
    .load = auth_window_load,
    .unload = auth_window_unload,
  });
  window_stack_push(s_app_data.auth_window, true);
}

void auth_window_destroy(void) {
  if (s_app_data.auth_window) {
    window_stack_pop(true);
    window_destroy(s_app_data.auth_window);
    s_app_data.auth_window = NULL;
  }
}

void auth_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  window_set_click_config_provider(window, auth_click_config_provider);
  
  TextLayer *title_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_LARGE, bounds.size.w - MARGIN_LARGE, TITLE_HEIGHT));
  text_layer_set_text_alignment(title_layer, GTextAlignmentCenter);
  text_layer_set_font(title_layer, fonts_get_system_font(FONT_KEY_TITLE));
  text_layer_set_text_color(title_layer, FOREGROUND_COLOR);
  text_layer_set_background_color(title_layer, GColorClear);
  text_layer_set_text(title_layer, DEFAULT_APP_NAME);
  layer_add_child(window_layer, text_layer_get_layer(title_layer));
  
  TextLayer *subtitle_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_LARGE + TITLE_HEIGHT + MARGIN_SMALL, bounds.size.w - MARGIN_LARGE, SUBTITLE_HEIGHT));
  text_layer_set_text_alignment(subtitle_layer, GTextAlignmentCenter);
  text_layer_set_font(subtitle_layer, fonts_get_system_font(FONT_KEY_SUBTITLE));
  text_layer_set_text_color(subtitle_layer, FOREGROUND_COLOR);
  text_layer_set_background_color(subtitle_layer, GColorClear);
  text_layer_set_text(subtitle_layer, DEFAULT_CONNECT_TEXT);
  layer_add_child(window_layer, text_layer_get_layer(subtitle_layer));
  
  TextLayer *instruction_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_LARGE + TITLE_HEIGHT + SUBTITLE_HEIGHT + MARGIN_MEDIUM, bounds.size.w - MARGIN_LARGE, INSTRUCTION_HEIGHT));
  text_layer_set_text_alignment(instruction_layer, GTextAlignmentCenter);
  text_layer_set_font(instruction_layer, fonts_get_system_font(FONT_KEY_SUBTITLE));
  text_layer_set_text_color(instruction_layer, FOREGROUND_COLOR);
  text_layer_set_background_color(instruction_layer, GColorClear);
  text_layer_set_text(instruction_layer, DEFAULT_AUTH_INSTRUCTION_TEXT);
  layer_add_child(window_layer, text_layer_get_layer(instruction_layer));
  
  window_set_background_color(window, AUTH_BACKGROUND_COLOR);
}

void auth_window_unload(Window *window) {
  // Text layers are created locally in auth_window_load and should be cleaned up automatically
  // when the window is destroyed. No explicit cleanup needed for local variables.
}

void auth_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, auth_click_handler);
}

void auth_click_handler(ClickRecognizerRef recognizer, void *context) {
  auth_request_authentication();
}

void auth_request_authentication(void) {
  s_app_data.auth_state = AUTH_STATE_REQUESTING;
  
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  dict_write_uint8(iter, MESSAGE_KEY_AUTH_REQUEST, 0);
  
  app_message_outbox_send();
}

void auth_handle_success(DictionaryIterator *iter) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Authentication successful, switching to main menu");
  s_app_data.auth_state = AUTH_STATE_AUTHENTICATED;
  s_app_data.is_authenticated = true;
  
  auth_window_destroy();
  
  s_app_data.current_state = APP_STATE_NOW_PLAYING;
  now_playing_window_create();
  
  spotify_api_refresh_now_playing();
}

void auth_handle_error(DictionaryIterator *iter) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Auth error received from JavaScript");
  s_app_data.auth_state = AUTH_STATE_ERROR;
  
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MESSAGE);
  if (error_tuple) {
    strncpy(s_app_data.auth_error, error_tuple->value->cstring, sizeof(s_app_data.auth_error) - 1);
    APP_LOG(APP_LOG_LEVEL_ERROR, "Auth error message: %s", s_app_data.auth_error);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "No error message found in auth error");
    strcpy(s_app_data.auth_error, "Authentication failed");
  }
  
  // Show auth window when authentication fails
  s_app_data.current_state = APP_STATE_AUTH_REQUIRED;
  auth_window_create();
}

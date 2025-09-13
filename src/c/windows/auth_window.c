#include "auth_window.h"
#include "../api/spotify_api.h"

void auth_window_init(void) {
  // Authentication window will be created when needed
}

void auth_window_deinit(void) {
  auth_window_destroy();
}

void auth_window_create(void) {
  if (s_app_data.auth_window) {
    return; // Already exists
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
  
  // Set up click recognizer
  window_set_click_config_provider(window, auth_click_config_provider);
  
  // Create text layers for authentication UI
  TextLayer *title_layer = text_layer_create(GRect(10, 20, bounds.size.w - 20, 30));
  text_layer_set_text_alignment(title_layer, GTextAlignmentCenter);
  text_layer_set_font(title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(title_layer, GColorWhite);
  text_layer_set_background_color(title_layer, GColorClear);
  text_layer_set_text(title_layer, "Pebblify");
  layer_add_child(window_layer, text_layer_get_layer(title_layer));
  
  TextLayer *subtitle_layer = text_layer_create(GRect(10, 50, bounds.size.w - 20, 40));
  text_layer_set_text_alignment(subtitle_layer, GTextAlignmentCenter);
  text_layer_set_font(subtitle_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(subtitle_layer, GColorWhite);
  text_layer_set_background_color(subtitle_layer, GColorClear);
  text_layer_set_text(subtitle_layer, "Connect to Spotify");
  layer_add_child(window_layer, text_layer_get_layer(subtitle_layer));
  
  TextLayer *instruction_layer = text_layer_create(GRect(10, 100, bounds.size.w - 20, 60));
  text_layer_set_text_alignment(instruction_layer, GTextAlignmentCenter);
  text_layer_set_font(instruction_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(instruction_layer, GColorWhite);
  text_layer_set_background_color(instruction_layer, GColorClear);
  text_layer_set_text(instruction_layer, "Press SELECT to authorize\nwith Spotify");
  layer_add_child(window_layer, text_layer_get_layer(instruction_layer));
  
  // Set background color
  window_set_background_color(window, GColorJaegerGreen);
}

void auth_window_unload(Window *window) {
  // Clean up if needed
}

void auth_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, auth_click_handler);
}

void auth_click_handler(ClickRecognizerRef recognizer, void *context) {
  auth_request_authentication();
}

void auth_request_authentication(void) {
  s_app_data.auth_state = AUTH_STATE_REQUESTING;
  
  // Send authentication request to JavaScript companion
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  // Send message_type as a string key with integer value
  dict_write_uint8(iter, 0, 0); // AUTH_REQUEST = 0
  
  app_message_outbox_send();
}

void auth_handle_success(DictionaryIterator *iter) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Authentication successful, switching to main menu");
  s_app_data.auth_state = AUTH_STATE_AUTHENTICATED;
  s_app_data.is_authenticated = true;
  
  // Extract tokens from message using the correct numeric keys
  Tuple *access_token_tuple = dict_find(iter, 7); // ACCESS_TOKEN
  Tuple *refresh_token_tuple = dict_find(iter, 8); // REFRESH_TOKEN
  Tuple *expires_at_tuple = dict_find(iter, 9);   // EXPIRES_AT
  
  if (access_token_tuple) {
    strncpy(s_app_data.access_token, access_token_tuple->value->cstring, sizeof(s_app_data.access_token) - 1);
    s_app_data.access_token[sizeof(s_app_data.access_token) - 1] = '\0';
  }
  if (refresh_token_tuple) {
    strncpy(s_app_data.refresh_token, refresh_token_tuple->value->cstring, sizeof(s_app_data.refresh_token) - 1);
    s_app_data.refresh_token[sizeof(s_app_data.refresh_token) - 1] = '\0';
  }
  if (expires_at_tuple) {
    s_app_data.token_expires_at = expires_at_tuple->value->uint32;
  }
  
  // Save authentication data to persistent storage
  app_state_save_auth_data();
  
  // Pop the auth window (main menu is already underneath)
  auth_window_destroy();
}

void auth_handle_error(DictionaryIterator *iter) {
  s_app_data.auth_state = AUTH_STATE_ERROR;
  
  Tuple *error_tuple = dict_find(iter, 1);
  if (error_tuple) {
    strncpy(s_app_data.auth_error, error_tuple->value->cstring, sizeof(s_app_data.auth_error) - 1);
  }
  
  // TODO: Show error message to user
}

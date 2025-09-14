#include "auth_window.h"
#include "../api/spotify_api.h"
#include "../core/app_state.h"
#include "../core/constants.h"
#include "message_keys.auto.h"

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
  
  // Set background color using compile-time macros
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
  
  // Send authentication request to JavaScript companion
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  // Send message_type as a string key with integer value
  dict_write_uint8(iter, MESSAGE_KEY_AUTH_REQUEST, 0); // AUTH_REQUEST
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "C->JS: Sending AUTH_REQUEST message");
  app_message_outbox_send();
}

void auth_handle_success(DictionaryIterator *iter) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "C: Received AUTH_SUCCESS message from JS");
  APP_LOG(APP_LOG_LEVEL_INFO, "Authentication successful, switching to main menu");
  s_app_data.auth_state = AUTH_STATE_AUTHENTICATED;
  s_app_data.is_authenticated = true;
  
  // Extract tokens from message using the correct message key constants
  Tuple *access_token_tuple = dict_find(iter, MESSAGE_KEY_ACCESS_TOKEN);
  Tuple *refresh_token_tuple = dict_find(iter, MESSAGE_KEY_REFRESH_TOKEN);
  Tuple *expires_at_tuple = dict_find(iter, MESSAGE_KEY_EXPIRES_AT);
  
  bool tokens_changed = false;
  
  if (access_token_tuple) {
    char new_access_token[256];
    strncpy(new_access_token, access_token_tuple->value->cstring, sizeof(new_access_token) - 1);
    new_access_token[sizeof(new_access_token) - 1] = '\0';
    
    // Compare with existing token
    if (strcmp(s_app_data.access_token, new_access_token) != 0) {
      strncpy(s_app_data.access_token, new_access_token, sizeof(s_app_data.access_token) - 1);
      s_app_data.access_token[sizeof(s_app_data.access_token) - 1] = '\0';
      tokens_changed = true;
      APP_LOG(APP_LOG_LEVEL_INFO, "Access token changed, length: %d", (int)strlen(s_app_data.access_token));
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "Access token unchanged, length: %d", (int)strlen(s_app_data.access_token));
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "No access token received");
  }
  
  if (refresh_token_tuple) {
    char new_refresh_token[256];
    strncpy(new_refresh_token, refresh_token_tuple->value->cstring, sizeof(new_refresh_token) - 1);
    new_refresh_token[sizeof(new_refresh_token) - 1] = '\0';
    
    // Compare with existing token
    if (strcmp(s_app_data.refresh_token, new_refresh_token) != 0) {
      strncpy(s_app_data.refresh_token, new_refresh_token, sizeof(s_app_data.refresh_token) - 1);
      s_app_data.refresh_token[sizeof(s_app_data.refresh_token) - 1] = '\0';
      tokens_changed = true;
      APP_LOG(APP_LOG_LEVEL_INFO, "Refresh token changed, length: %d", (int)strlen(s_app_data.refresh_token));
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "Refresh token unchanged, length: %d", (int)strlen(s_app_data.refresh_token));
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "No refresh token received");
  }
  
  if (expires_at_tuple) {
    uint32_t new_expires_at = expires_at_tuple->value->uint32;
    if ((uint32_t)s_app_data.token_expires_at != new_expires_at) {
      s_app_data.token_expires_at = (time_t)new_expires_at;
      tokens_changed = true;
      APP_LOG(APP_LOG_LEVEL_INFO, "Token expiration changed to: %lu", (unsigned long)s_app_data.token_expires_at);
    } else {
      APP_LOG(APP_LOG_LEVEL_INFO, "Token expiration unchanged: %lu", (unsigned long)s_app_data.token_expires_at);
    }
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "No expiration time received");
  }
  
  // Only save to persistent storage if tokens actually changed
  if (tokens_changed) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Tokens changed, saving to persistent storage");
    app_state_save_auth_data();
  } else {
    APP_LOG(APP_LOG_LEVEL_INFO, "Tokens unchanged, skipping persistent storage update");
  }
  
  // Pop the auth window (main menu is already underneath)
  auth_window_destroy();
  
  // Refresh now playing data after successful authentication
  spotify_api_refresh_now_playing();
}

void auth_handle_error(DictionaryIterator *iter) {
  s_app_data.auth_state = AUTH_STATE_ERROR;
  
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MESSAGE);
  if (error_tuple) {
    strncpy(s_app_data.auth_error, error_tuple->value->cstring, sizeof(s_app_data.auth_error) - 1);
  }
  
  // TODO: Show error message to user
}

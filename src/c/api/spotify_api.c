#include "spotify_api.h"
#include "../windows/now_playing.h"
#include "../windows/auth_window.h"
#include "../core/constants.h"
#include "message_keys.auto.h"

void spotify_api_init(void) {
  // Set up AppMessage
  app_message_register_inbox_received(spotify_api_app_message_handler);
  app_message_register_outbox_failed(spotify_api_app_message_outbox_failed);
  app_message_register_outbox_sent(spotify_api_app_message_outbox_sent);
  
  const uint32_t inbox_size = INBOX_SIZE;
  const uint32_t outbox_size = OUTBOX_SIZE;
  app_message_open(inbox_size, outbox_size);
}

void spotify_api_deinit(void) {
  // AppMessage cleanup is handled automatically
}

void spotify_api_make_call(const char *path, const char *method, const char *data) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  dict_write_uint8(iter, MESSAGE_KEY_API_CALL, 1); // API_CALL message type
  dict_write_cstring(iter, MESSAGE_KEY_API_PATH, path); // API_PATH
  dict_write_cstring(iter, MESSAGE_KEY_HTTP_METHOD, method); // HTTP_METHOD
  if (data) {
    dict_write_cstring(iter, MESSAGE_KEY_API_DATA, data); // API_DATA
  }
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "C->JS: Sending API_CALL message: %s %s", method, path);
  // APP_LOG(APP_LOG_LEVEL_INFO, "C: MESSAGE_KEY_API_CALL = %lu", (unsigned long)MESSAGE_KEY_API_CALL);
  // APP_LOG(APP_LOG_LEVEL_INFO, "C: MESSAGE_KEY_API_PATH = %lu", (unsigned long)MESSAGE_KEY_API_PATH);
  // APP_LOG(APP_LOG_LEVEL_INFO, "C: MESSAGE_KEY_HTTP_METHOD = %lu", (unsigned long)MESSAGE_KEY_HTTP_METHOD);
  app_message_outbox_send();
}

void spotify_api_refresh_now_playing(void) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "refresh_now_playing called");
  spotify_api_make_call("/me/player", "GET", NULL);
  
  // Note: Timer management is handled by the now_playing module
  // This function should not create its own timers to avoid conflicts
}

void spotify_api_play_pause_track(void) {
  // Send action message key to JS instead of composing URL
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending ACTION=play_pause message to JS");
  dict_write_cstring(iter, MESSAGE_KEY_ACTION, "play_pause");
  
  app_message_outbox_send();
}

void spotify_api_skip_to_next(void) {
  // Send action message key to JS instead of composing URL
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending ACTION=skip_next message to JS");
  dict_write_cstring(iter, MESSAGE_KEY_ACTION, "skip_next");
  
  app_message_outbox_send();
}

void spotify_api_skip_to_previous(void) {
  // Send action message key to JS instead of composing URL
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending ACTION=skip_prev message to JS");
  dict_write_cstring(iter, MESSAGE_KEY_ACTION, "skip_prev");
  
  app_message_outbox_send();
}

void spotify_api_volume_up(void) {
  // Track which button was pressed for error handling
  s_app_data.volume_error_button = BUTTON_ID_UP;
  
  // Send action message key to JS instead of composing URL
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending ACTION=volume_up message to JS");
  dict_write_cstring(iter, MESSAGE_KEY_ACTION, "volume_up");
  
  app_message_outbox_send();
}

void spotify_api_volume_down(void) {
  // Track which button was pressed for error handling
  s_app_data.volume_error_button = BUTTON_ID_DOWN;
  
  // Send action message key to JS instead of composing URL
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Sending ACTION=volume_down message to JS");
  dict_write_cstring(iter, MESSAGE_KEY_ACTION, "volume_down");
  
  app_message_outbox_send();
}

// Keep the old function for backward compatibility, but redirect to new functions
void spotify_api_set_volume(int volume_percent, ButtonId button) {
  // This function is now deprecated - use spotify_api_volume_up/down instead
  // For now, we'll just store the button for error handling
  s_app_data.volume_error_button = button;
}

void spotify_api_handle_response(DictionaryIterator *iter) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Received API response");
  
  // Get parsed data from JavaScript
  Tuple *track_name_tuple = dict_find(iter, MESSAGE_KEY_TRACK_NAME);
  Tuple *artist_name_tuple = dict_find(iter, MESSAGE_KEY_ARTIST_NAME);
  Tuple *is_playing_tuple = dict_find(iter, MESSAGE_KEY_IS_PLAYING);
  Tuple *volume_tuple = dict_find(iter, MESSAGE_KEY_VOLUME_PERCENT);
  Tuple *can_skip_prev_tuple = dict_find(iter, MESSAGE_KEY_CAN_SKIP_PREV);
  Tuple *can_skip_next_tuple = dict_find(iter, MESSAGE_KEY_CAN_SKIP_NEXT);
  
  // Update track name only if provided (preserve existing data if not)
  if (track_name_tuple && track_name_tuple->value->cstring && strlen(track_name_tuple->value->cstring) > 0) {
    strncpy(s_app_data.track_name, track_name_tuple->value->cstring, sizeof(s_app_data.track_name) - 1);
    s_app_data.track_name[sizeof(s_app_data.track_name) - 1] = '\0';
    // APP_LOG(APP_LOG_LEVEL_INFO, "Track name: %s", s_app_data.track_name);
  }
  
  // Update artist name only if provided (preserve existing data if not)
  if (artist_name_tuple && artist_name_tuple->value->cstring && strlen(artist_name_tuple->value->cstring) > 0) {
    strncpy(s_app_data.artist_name, artist_name_tuple->value->cstring, sizeof(s_app_data.artist_name) - 1);
    s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
    // APP_LOG(APP_LOG_LEVEL_INFO, "Artist name: %s", s_app_data.artist_name);
  }
  
  // Update playing status
  if (is_playing_tuple) {
    s_app_data.is_playing = (is_playing_tuple->value->uint8 == 1);
    // APP_LOG(APP_LOG_LEVEL_INFO, "Is playing: %s", s_app_data.is_playing ? "true" : "false");
  }
  
  // Update volume with aggressive stale data detection
  if (volume_tuple) {
    int api_volume = volume_tuple->value->uint8;
    
    // Always detect suspicious volume jumps, not just during pending changes
    bool is_suspicious = false;
    if (s_app_data.last_known_volume >= 0) {
      int volume_diff = abs(api_volume - s_app_data.last_known_volume);
      
      // Detect the specific "50 bug" - if API suddenly returns 50, it's likely stale
      if (api_volume == VOLUME_DEFAULT && s_app_data.last_known_volume != VOLUME_DEFAULT) {
        is_suspicious = true;
        APP_LOG(APP_LOG_LEVEL_INFO, "Detected '50 bug' - API returned %d, last known was %d", VOLUME_DEFAULT, s_app_data.last_known_volume);
      }
      
      // Detect large unexpected jumps (more than 15% change without a pending change)
      if (volume_diff > VOLUME_SUSPICIOUS_THRESHOLD && !s_app_data.volume_change_pending) {
        is_suspicious = true;
        APP_LOG(APP_LOG_LEVEL_INFO, "Detected large volume jump (%d -> %d) without pending change", s_app_data.last_known_volume, api_volume);
      }
      
      // During pending changes, be more strict about what we accept
      if (s_app_data.volume_change_pending && volume_diff > VOLUME_PENDING_THRESHOLD && abs(s_app_data.pending_volume_delta) <= VOLUME_PENDING_THRESHOLD) {
        is_suspicious = true;
        APP_LOG(APP_LOG_LEVEL_INFO, "Detected unexpected volume change during pending operation");
      }
    }
    
    if (is_suspicious) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Ignoring stale volume data (%d), keeping local volume %d", api_volume, s_app_data.last_known_volume);
      // Keep current local volume, don't update from API
    } else {
      s_app_data.volume_percent = api_volume;
      s_app_data.last_known_volume = api_volume; // Update last known good volume
      // APP_LOG(APP_LOG_LEVEL_INFO, "Volume from API: %d%%", s_app_data.volume_percent);
    }
  }
  
  // Update skip permissions
  if (can_skip_prev_tuple) {
    s_app_data.can_skip_prev = (can_skip_prev_tuple->value->uint8 == 1);
  }
  if (can_skip_next_tuple) {
    s_app_data.can_skip_next = (can_skip_next_tuple->value->uint8 == 1);
  }
  
  s_app_data.is_active_session = true;
  
  // Update display if now playing window is active
  if (s_app_data.now_playing_window) {
    now_playing_update_display();
  }
}

void spotify_api_handle_error(DictionaryIterator *iter) {
  Tuple *error_tuple = dict_find(iter, MESSAGE_KEY_ERROR_MESSAGE);
  if (error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "API Error: %s", error_tuple->value->cstring);
    
    // Check for specific error types
    if (strstr(error_tuple->value->cstring, "403") || strstr(error_tuple->value->cstring, "Action not allowed")) {
      // Volume control failed - show X icon on the button that failed
      APP_LOG(APP_LOG_LEVEL_INFO, "Volume control failed, showing error for button %d", s_app_data.volume_error_button);
      now_playing_show_volume_error(s_app_data.volume_error_button);
    } else if (strstr(error_tuple->value->cstring, "401")) {
      // Authentication error - token may be expired
      strcpy(s_app_data.track_name, "Authentication expired");
      strcpy(s_app_data.artist_name, "Please re-authenticate");
      s_app_data.is_active_session = false;
    } else if (strstr(error_tuple->value->cstring, "404")) {
      // No active device or track
      strcpy(s_app_data.track_name, "No Active Device");
      strcpy(s_app_data.artist_name, "Start playing music on Spotify");
      s_app_data.is_active_session = false;
    } else {
      // Other API errors
      s_app_data.is_active_session = false;
      strcpy(s_app_data.track_name, "Connection error");
      strcpy(s_app_data.artist_name, "Check your internet connection");
    }
    
    if (s_app_data.now_playing_window) {
      now_playing_update_display();
    }
  }
}

void spotify_api_app_message_handler(DictionaryIterator *iter, void *context) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "C: Received message from JS");
  
  // Check for different message types by looking at the keys
  Tuple *auth_success_tuple = dict_find(iter, MESSAGE_KEY_AUTH_SUCCESS);
  Tuple *auth_error_tuple = dict_find(iter, MESSAGE_KEY_AUTH_ERROR);
  Tuple *api_response_tuple = dict_find(iter, MESSAGE_KEY_API_RESPONSE);
  Tuple *api_error_tuple = dict_find(iter, MESSAGE_KEY_API_ERROR);
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Message received - auth_success: %d, auth_error: %d, api_response: %d, api_error: %d", 
  //         auth_success_tuple ? 1 : 0, auth_error_tuple ? 1 : 0, api_response_tuple ? 1 : 0, api_error_tuple ? 1 : 0);
  
  if (auth_success_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Calling handle_auth_success");
    auth_handle_success(iter);
  } else if (auth_error_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Calling handle_auth_error");
    auth_handle_error(iter);
  } else if (api_response_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Handling API response");
    spotify_api_handle_response(iter);
  } else if (api_error_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Handling API error");
    spotify_api_handle_error(iter);
  } else {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Unknown message type received");
  }
}

void spotify_api_app_message_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  // Handle outbox failure
  s_app_data.auth_state = AUTH_STATE_ERROR;
  strcpy(s_app_data.auth_error, "Failed to send message");
}

void spotify_api_app_message_outbox_sent(DictionaryIterator *iter, void *context) {
  // Message sent successfully
}

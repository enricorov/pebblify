#include "spotify_api.h"
#include "../windows/now_playing.h"
#include "../windows/auth_window.h"

void spotify_api_init(void) {
  // Set up AppMessage
  app_message_register_inbox_received(spotify_api_app_message_handler);
  app_message_register_outbox_failed(spotify_api_app_message_outbox_failed);
  app_message_register_outbox_sent(spotify_api_app_message_outbox_sent);
  
  const uint32_t inbox_size = 1024;
  const uint32_t outbox_size = 1024;
  app_message_open(inbox_size, outbox_size);
}

void spotify_api_deinit(void) {
  // AppMessage cleanup is handled automatically
}

void spotify_api_make_call(const char *path, const char *method, const char *data) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  dict_write_uint8(iter, 4, 1); // API_CALL message type (key 4, value 1)
  dict_write_cstring(iter, 10, path); // API_PATH (key 10)
  dict_write_cstring(iter, 11, method); // HTTP_METHOD (key 11)
  if (data) {
    dict_write_cstring(iter, 12, data); // API_DATA (key 12)
  }
  
  // Debug: Log the API call being made
  // APP_LOG(APP_LOG_LEVEL_INFO, "Making Spotify API call: %s %s", method, path);
  
  app_message_outbox_send();
}

void spotify_api_refresh_now_playing(void) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "refresh_now_playing called");
  spotify_api_make_call("/me/player", "GET", NULL);
  
  // Note: Timer management is handled by the now_playing module
  // This function should not create its own timers to avoid conflicts
}

void spotify_api_play_pause_track(void) {
  const char *action = s_app_data.is_playing ? "pause" : "play";
  char path[64];
  snprintf(path, sizeof(path), "/me/player/%s", action);
  spotify_api_make_call(path, "PUT", NULL);
  // Refresh now playing data after play/pause
  // Note: This is a one-shot timer that will clean itself up
  app_timer_register(500, (AppTimerCallback)spotify_api_refresh_now_playing, NULL);
}

void spotify_api_skip_to_next(void) {
  spotify_api_make_call("/me/player/next", "POST", NULL);
  // Refresh now playing data after skipping
  // Note: This is a one-shot timer that will clean itself up
  app_timer_register(500, (AppTimerCallback)spotify_api_refresh_now_playing, NULL);
}

void spotify_api_skip_to_previous(void) {
  spotify_api_make_call("/me/player/previous", "POST", NULL);
  // Refresh now playing data after skipping
  // Note: This is a one-shot timer that will clean itself up
  app_timer_register(500, (AppTimerCallback)spotify_api_refresh_now_playing, NULL);
}

void spotify_api_set_volume(int volume_percent, ButtonId button) {
  char path[128];
  snprintf(path, sizeof(path), "/me/player/volume?volume_percent=%d", volume_percent);
  // APP_LOG(APP_LOG_LEVEL_INFO, "Setting volume to %d%%", volume_percent);
  
  // Store which button was pressed for error handling
  s_app_data.volume_error_button = button;
  
  spotify_api_make_call(path, "PUT", NULL);
}

void spotify_api_handle_response(DictionaryIterator *iter) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Received API response");
  
  // Get parsed data from JavaScript
  Tuple *track_name_tuple = dict_find(iter, 15); // TRACK_NAME key
  Tuple *artist_name_tuple = dict_find(iter, 16); // ARTIST_NAME key
  Tuple *is_playing_tuple = dict_find(iter, 17); // IS_PLAYING key
  Tuple *volume_tuple = dict_find(iter, 18); // VOLUME_PERCENT key
  Tuple *can_skip_prev_tuple = dict_find(iter, 19); // CAN_SKIP_PREV key
  Tuple *can_skip_next_tuple = dict_find(iter, 20); // CAN_SKIP_NEXT key
  
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
      if (api_volume == 50 && s_app_data.last_known_volume != 50) {
        is_suspicious = true;
        APP_LOG(APP_LOG_LEVEL_INFO, "Detected '50 bug' - API returned 50, last known was %d", s_app_data.last_known_volume);
      }
      
      // Detect large unexpected jumps (more than 15% change without a pending change)
      if (volume_diff > 15 && !s_app_data.volume_change_pending) {
        is_suspicious = true;
        APP_LOG(APP_LOG_LEVEL_INFO, "Detected large volume jump (%d -> %d) without pending change", s_app_data.last_known_volume, api_volume);
      }
      
      // During pending changes, be more strict about what we accept
      if (s_app_data.volume_change_pending && volume_diff > 10 && abs(s_app_data.pending_volume_delta) <= 10) {
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
  
  // If we have a pending volume change, apply it now that we have current volume
  if (s_app_data.volume_change_pending) {
    now_playing_apply_volume_change();
    return; // Skip normal display update since apply_volume_change handles it
  }
  
  // Update display if now playing window is active
  if (s_app_data.now_playing_window) {
    now_playing_update_display();
  }
}

void spotify_api_handle_error(DictionaryIterator *iter) {
  Tuple *error_tuple = dict_find(iter, 13); // ERROR_MESSAGE key
  if (error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "API Error: %s", error_tuple->value->cstring);
    
    // Check for specific error types
    if (strstr(error_tuple->value->cstring, "403")) {
      // Volume control failed - show X icon on the button that failed
      APP_LOG(APP_LOG_LEVEL_INFO, "Volume control failed (403), showing error for button %d", s_app_data.volume_error_button);
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
  // Check for different message types by looking at the keys
  Tuple *auth_success_tuple = dict_find(iter, 1); // AUTH_SUCCESS
  Tuple *auth_error_tuple = dict_find(iter, 2);   // AUTH_ERROR
  Tuple *api_response_tuple = dict_find(iter, 5); // API_RESPONSE
  Tuple *api_error_tuple = dict_find(iter, 6);    // API_ERROR
  
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

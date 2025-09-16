#include "app_state.h"

// Global app data instance
AppData s_app_data;

void app_state_init(void) {
  memset(&s_app_data, 0, sizeof(AppData));
  s_app_data.current_state = APP_STATE_AUTH_REQUIRED;
  s_app_data.auth_state = AUTH_STATE_NONE;
  s_app_data.last_known_volume = -1;
}

void app_state_deinit(void) {
  if (s_app_data.main_window) {
    window_destroy(s_app_data.main_window);
  }
  if (s_app_data.now_playing_window) {
    window_destroy(s_app_data.now_playing_window);
  }
  if (s_app_data.playlists_window) {
    window_destroy(s_app_data.playlists_window);
  }
  if (s_app_data.auth_window) {
    window_destroy(s_app_data.auth_window);
  }
  
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
  
  if (s_app_data.cached_vol_up_bitmap) {
    gbitmap_destroy(s_app_data.cached_vol_up_bitmap);
  }
  if (s_app_data.cached_vol_down_bitmap) {
    gbitmap_destroy(s_app_data.cached_vol_down_bitmap);
  }
  if (s_app_data.cached_play_bitmap) {
    gbitmap_destroy(s_app_data.cached_play_bitmap);
  }
  if (s_app_data.cached_pause_bitmap) {
    gbitmap_destroy(s_app_data.cached_pause_bitmap);
  }
  if (s_app_data.cached_backward_bitmap) {
    gbitmap_destroy(s_app_data.cached_backward_bitmap);
  }
  if (s_app_data.cached_forward_bitmap) {
    gbitmap_destroy(s_app_data.cached_forward_bitmap);
  }
  if (s_app_data.cached_dismiss_bitmap) {
    gbitmap_destroy(s_app_data.cached_dismiss_bitmap);
  }
}

void app_state_save_auth_data(void) {
  if (s_app_data.is_authenticated && strlen(s_app_data.access_token) > 0) {
    persist_write_string(1, s_app_data.access_token);
    persist_write_string(2, s_app_data.refresh_token);
    persist_write_int(3, s_app_data.token_expires_at);
    persist_write_bool(4, true);
    APP_LOG(APP_LOG_LEVEL_INFO, "Authentication data saved to persistent storage");
    APP_LOG(APP_LOG_LEVEL_INFO, "Token length: %d", (int)strlen(s_app_data.access_token));
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Cannot save auth data - not authenticated or no token");
  }
}

void app_state_load_auth_data(void) {
  if (persist_exists(4) && persist_read_bool(4)) {
    persist_read_string(1, s_app_data.access_token, sizeof(s_app_data.access_token));
    persist_read_string(2, s_app_data.refresh_token, sizeof(s_app_data.refresh_token));
    s_app_data.token_expires_at = persist_read_int(3);
    s_app_data.is_authenticated = true;
    s_app_data.auth_state = AUTH_STATE_AUTHENTICATED;
    APP_LOG(APP_LOG_LEVEL_INFO, "Authentication data loaded from persistent storage");
    APP_LOG(APP_LOG_LEVEL_INFO, "Token length: %d", (int)strlen(s_app_data.access_token));
    APP_LOG(APP_LOG_LEVEL_INFO, "Token expires at: %lu", s_app_data.token_expires_at);
  } else {
    s_app_data.is_authenticated = false;
    s_app_data.auth_state = AUTH_STATE_NONE;
    APP_LOG(APP_LOG_LEVEL_INFO, "No authentication data found in persistent storage");
  }
}

void app_state_clear_auth_data(void) {
  persist_delete(1);
  persist_delete(2);
  persist_delete(3);
  persist_delete(4);
}

GBitmap* app_state_get_cached_bitmap(GBitmap **cached_bitmap, uint32_t resource_id) {
  if (*cached_bitmap) {
    return *cached_bitmap;
  }
  
  *cached_bitmap = gbitmap_create_with_resource(resource_id);
  if (!*cached_bitmap) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load bitmap resource %u", (unsigned int)resource_id);
  }
  
  return *cached_bitmap;
}
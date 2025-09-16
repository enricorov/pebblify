#include "app_state.h"
#include "../windows/auth_window.h"
#include "../windows/now_playing.h"

// Global app data instance
AppData s_app_data;

void app_state_init(void) {
  memset(&s_app_data, 0, sizeof(AppData));
  s_app_data.current_state = APP_STATE_NOW_PLAYING;
  s_app_data.auth_state = AUTH_STATE_REQUESTING;
  s_app_data.last_known_volume = -1;
  
  // Authentication state is managed by JavaScript side
  // Start with unauthenticated state, then immediately request auth status
  s_app_data.is_authenticated = false;
  
  // Request authentication status from JavaScript immediately
  auth_request_authentication();
  
  // Show now playing window by default - auth window will show if auth fails
  now_playing_window_create();
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
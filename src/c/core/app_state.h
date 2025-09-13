#ifndef APP_STATE_H
#define APP_STATE_H

#include <pebble.h>
#include <time.h>

// App state
typedef enum {
  APP_STATE_AUTH_REQUIRED,
  APP_STATE_AUTHENTICATING,
  APP_STATE_MAIN_MENU,
  APP_STATE_NOW_PLAYING,
  APP_STATE_PLAYLISTS
} AppState;

// Authentication state
typedef enum {
  AUTH_STATE_NONE,
  AUTH_STATE_REQUESTING,
  AUTH_STATE_AUTHENTICATED,
  AUTH_STATE_ERROR
} AuthState;

// Core app data structure
typedef struct {
  AppState current_state;
  AuthState auth_state;
  Window *main_window;
  Window *now_playing_window;
  Window *playlists_window;
  Window *auth_window;
  bool is_authenticated;
  char access_token[512];
  char refresh_token[512];
  time_t token_expires_at;
  char auth_error[128];
  
  // Now playing data
  bool is_active_session;
  char track_name[128];
  char artist_name[128];
  bool is_playing;
  int volume_percent;
  bool can_skip_prev;
  bool can_skip_next;
  
  // Volume control state
  bool volume_change_pending;
  ButtonId pending_volume_button;
  int pending_volume_delta;
  time_t last_volume_fetch_time;
  int last_known_volume; // Track last known good volume
  
  // Volume control error state
  ButtonId volume_error_button;
  AppTimer *volume_error_timer;
  bool showing_volume_error;
  
  // Bitmap caching to prevent PNG errors
  GBitmap *cached_vol_up_bitmap;
  GBitmap *cached_vol_down_bitmap;
  GBitmap *cached_play_bitmap;
  GBitmap *cached_pause_bitmap;
  GBitmap *cached_backward_bitmap;
  GBitmap *cached_forward_bitmap;
  GBitmap *cached_dismiss_bitmap;
} AppData;

// Global app data instance
extern AppData s_app_data;

// App state management functions
void app_state_init(void);
void app_state_deinit(void);
void app_state_save_auth_data(void);
void app_state_load_auth_data(void);
void app_state_clear_auth_data(void);

// Bitmap caching functions
GBitmap* app_state_get_cached_bitmap(GBitmap **cached_bitmap, uint32_t resource_id);

#endif // APP_STATE_H

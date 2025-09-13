#include <pebble.h>
#include "core/app_state.h"
#include "windows/main_menu.h"
#include "windows/auth_window.h"
#include "windows/now_playing.h"
#include "api/spotify_api.h"

// Global back button handler
void back_button_handler(ClickRecognizerRef recognizer, void *context) {
  // Handle back button based on current app state
  if (s_app_data.current_state == APP_STATE_NOW_PLAYING) {
    // Pop the now playing window instead of destroying it
    now_playing_window_pop();
  } else if (s_app_data.current_state == APP_STATE_PLAYLISTS) {
    // TODO: Handle playlists back button
    s_app_data.current_state = APP_STATE_MAIN_MENU;
  } else if (s_app_data.current_state == APP_STATE_MAIN_MENU) {
    // On main menu, back button does nothing (user can navigate away from app)
    // Pebble apps typically don't exit programmatically
  }
  // For auth state, back button does nothing
}

void click_config_provider(void *context) {
  // Register back button handler
  window_single_click_subscribe(BUTTON_ID_BACK, back_button_handler);
}

int main(void) {
  // Initialize all modules
  app_state_init();
  spotify_api_init();
  main_menu_init();
  auth_window_init();
  now_playing_init();
  
  // Load authentication data from persistent storage
  app_state_load_auth_data();
  
  // Always create main window first (this will be the base layer)
  s_app_data.current_state = APP_STATE_MAIN_MENU;
  
  if (s_app_data.auth_state != AUTH_STATE_AUTHENTICATED) {
    // Not authenticated - create auth window on top of main window
    auth_window_create();
  }
  
  // Start the app event loop
  app_event_loop();
  
  // Clean up all modules
  now_playing_deinit();
  auth_window_deinit();
  main_menu_deinit();
  spotify_api_deinit();
  app_state_deinit();
}

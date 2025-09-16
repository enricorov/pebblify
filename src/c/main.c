#include <pebble.h>
#include "core/app_state.h"
#include "windows/main_menu.h"
#include "windows/auth_window.h"
#include "windows/now_playing.h"
#include "api/spotify_api.h"

void back_button_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_app_data.current_state == APP_STATE_NOW_PLAYING) {
    now_playing_window_pop();
  } else if (s_app_data.current_state == APP_STATE_PLAYLISTS) {
    // TODO: Handle playlists back button
    s_app_data.current_state = APP_STATE_MAIN_MENU;
  }
  // Auth state and main menu: back button does nothing
}

void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_button_handler);
}

int main(void) {
  app_state_init();
  spotify_api_init();
  auth_window_init();
  now_playing_init();
  app_state_load_auth_data();
  main_menu_init();
  
  if (s_app_data.auth_state == AUTH_STATE_AUTHENTICATED) {
    s_app_data.current_state = APP_STATE_NOW_PLAYING;
    now_playing_window_create();
  } else {
    s_app_data.current_state = APP_STATE_AUTH_REQUIRED;
    auth_window_create();
  }
  
  app_event_loop();
  
  now_playing_deinit();
  auth_window_deinit();
  main_menu_deinit();
  spotify_api_deinit();
  app_state_deinit();
}

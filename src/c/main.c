#include <pebble.h>
#include <stdio.h>
#include <string.h>
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

// Now playing control modes

typedef struct {
  AppState current_state;
  AuthState auth_state;
  Window *main_window;
  MenuLayer *main_menu;
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
  
  // Now playing UI elements
  TextLayer *track_layer;
  TextLayer *artist_layer;
  TextLayer *clock_layer;
  ActionBarLayer *action_bar_layer;
  AppTimer *refresh_timer;
  AppTimer *clock_timer;
  
  // Volume control error state
  ButtonId volume_error_button;
  AppTimer *volume_error_timer;
  bool showing_volume_error;
  
  // Volume control state
  bool volume_change_pending;
  ButtonId pending_volume_button;
  int pending_volume_delta;
  time_t last_volume_fetch_time;
  int last_known_volume; // Track last known good volume
} AppData;

static AppData s_app_data;

// Forward declarations
static void init_app(void);
static void deinit_app(void);
static void main_window_load(Window *window);
static void main_window_unload(Window *window);
static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data);
static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data);
static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data);
static void menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data);
static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data);
static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data);

// Authentication functions
static void auth_window_load(Window *window);
static void auth_window_unload(Window *window);
static void auth_click_handler(ClickRecognizerRef recognizer, void *context);
static void auth_click_config_provider(void *context);
static void request_authentication(void);
static void handle_auth_success(DictionaryIterator *iter);
static void handle_auth_error(DictionaryIterator *iter);
static void handle_api_response(DictionaryIterator *iter);
static void handle_api_error(DictionaryIterator *iter);
static void app_message_handler(DictionaryIterator *iter, void *context);
static void app_message_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context);
static void app_message_outbox_sent(DictionaryIterator *iter, void *context);
static void make_spotify_api_call(const char *path, const char *method, const char *data);
static void refresh_now_playing(void);
static void play_pause_track(void);
static void skip_to_next(void);
static void skip_to_previous(void);
static void set_volume(int volume_percent, ButtonId button);
static int calculate_text_height(const char *text, GFont font, int width);

// Now playing window functions
static void now_playing_window_load(Window *window);
static void now_playing_window_unload(Window *window);
static void now_playing_click_handler(ClickRecognizerRef recognizer, void *context);
static void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *context);
static void now_playing_click_config_provider(void *context);
static void now_playing_update_display(void);
static void now_playing_handle_action(ButtonId button);
static void update_clock(void);
static void show_volume_error(ButtonId button);
static void clear_volume_error(void);
static void request_volume_change(ButtonId button, int delta);
static void apply_volume_change(void);

int main(void) {
  init_app();
  app_event_loop();
  deinit_app();
}

static void save_auth_data(void) {
  // Save authentication data to persistent storage
  if (s_app_data.is_authenticated && strlen(s_app_data.access_token) > 0) {
    persist_write_string(1, s_app_data.access_token);
    persist_write_string(2, s_app_data.refresh_token);
    persist_write_int(3, s_app_data.token_expires_at);
    persist_write_bool(4, true);
    // APP_LOG(APP_LOG_LEVEL_INFO, "Authentication data saved to persistent storage");
  }
}

static void load_auth_data(void) {
  // Load authentication data from persistent storage
  if (persist_exists(4) && persist_read_bool(4)) {
    persist_read_string(1, s_app_data.access_token, sizeof(s_app_data.access_token));
    persist_read_string(2, s_app_data.refresh_token, sizeof(s_app_data.refresh_token));
    s_app_data.token_expires_at = persist_read_int(3);
    s_app_data.is_authenticated = true;
    s_app_data.auth_state = AUTH_STATE_AUTHENTICATED;
    // APP_LOG(APP_LOG_LEVEL_INFO, "Authentication data loaded from persistent storage");
  } else {
    s_app_data.is_authenticated = false;
    s_app_data.auth_state = AUTH_STATE_NONE;
    // APP_LOG(APP_LOG_LEVEL_INFO, "No authentication data found in persistent storage");
  }
}

static void clear_auth_data(void) {
  // Clear authentication data from persistent storage
  persist_delete(1);
  persist_delete(2);
  persist_delete(3);
  persist_delete(4);
  // APP_LOG(APP_LOG_LEVEL_INFO, "Authentication data cleared from persistent storage");
}

static void init_app(void) {
  // Initialize app data
  memset(&s_app_data, 0, sizeof(AppData));
  s_app_data.current_state = APP_STATE_AUTH_REQUIRED;
  s_app_data.auth_state = AUTH_STATE_NONE;
  s_app_data.last_known_volume = -1; // Initialize to invalid value
  
  // Set up AppMessage
  app_message_register_inbox_received(app_message_handler);
  app_message_register_outbox_failed(app_message_outbox_failed);
  app_message_register_outbox_sent(app_message_outbox_sent);
  
  const uint32_t inbox_size = 1024;
  const uint32_t outbox_size = 1024;
  app_message_open(inbox_size, outbox_size);
  
  // Load authentication data from persistent storage
  load_auth_data();
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Auth state after load: %d, is_authenticated: %s", 
  //         s_app_data.auth_state, s_app_data.is_authenticated ? "true" : "false");
  
  // Always create main window first (this will be the base layer)
  // APP_LOG(APP_LOG_LEVEL_INFO, "Creating main window as base layer");
  s_app_data.current_state = APP_STATE_MAIN_MENU;
  s_app_data.main_window = window_create();
  window_set_window_handlers(s_app_data.main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_app_data.main_window, true);
  
  if (s_app_data.auth_state != AUTH_STATE_AUTHENTICATED) {
    // Not authenticated - create auth window on top of main window
    // APP_LOG(APP_LOG_LEVEL_INFO, "Not authenticated, creating auth window on top");
    s_app_data.auth_window = window_create();
    window_set_window_handlers(s_app_data.auth_window, (WindowHandlers) {
      .load = auth_window_load,
      .unload = auth_window_unload,
    });
    window_stack_push(s_app_data.auth_window, true);
  } else {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Already authenticated, main window is visible");
  }
}

static void deinit_app(void) {
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
}

static void main_window_load(Window *window) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Main window load called");
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create main menu
  s_app_data.main_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_app_data.main_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = menu_get_num_sections_callback,
    .get_num_rows = menu_get_num_rows_callback,
    .get_header_height = menu_get_header_height_callback,
    .draw_header = menu_draw_header_callback,
    .draw_row = menu_draw_row_callback,
    .select_click = menu_select_callback,
  });
  
  menu_layer_set_click_config_onto_window(s_app_data.main_menu, window);
  
  // Enable clock in menu layer
  menu_layer_set_highlight_colors(s_app_data.main_menu, GColorBlack, GColorWhite);
  
  layer_add_child(window_layer, menu_layer_get_layer(s_app_data.main_menu));
}

static void main_window_unload(Window *window) {
  if (s_app_data.main_menu) {
    menu_layer_destroy(s_app_data.main_menu);
  }
}

static uint16_t menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 3; // Home, Library, Devices
}

static uint16_t menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case 0: return 3; // Home: Now playing, Jump back in, Made for you
    case 1: return 3; // Library: Playlists, Albums, Artists
    case 2: return 1; // Devices: Play on device
    default: return 0;
  }
}

static int16_t menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

static void menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  const char *headers[] = {"Home", "Library", "Devices"};
  menu_cell_basic_header_draw(ctx, cell_layer, headers[section_index]);
}

static void menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  const char *items[3][3] = {
    {"Now playing", "Jump back in", "Made for you"},
    {"Playlists", "Albums", "Artists"},
    {"Play on device", "", ""}
  };
  
  menu_cell_basic_draw(ctx, cell_layer, items[cell_index->section][cell_index->row], NULL, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  // Handle menu selection
  if (cell_index->section == 0 && cell_index->row == 0) {
    // Now playing selected
    s_app_data.current_state = APP_STATE_NOW_PLAYING;
    
    // Create now playing window
    s_app_data.now_playing_window = window_create();
    window_set_window_handlers(s_app_data.now_playing_window, (WindowHandlers) {
      .load = now_playing_window_load,
      .unload = now_playing_window_unload,
    });
    
    window_stack_push(s_app_data.now_playing_window, true);
  } else if (cell_index->section == 1 && cell_index->row == 0) {
    // Playlists selected
    s_app_data.current_state = APP_STATE_PLAYLISTS;
    // TODO: Show playlists window
  }
}

// Now playing window implementation
static void now_playing_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create ActionBarLayer with animation
  s_app_data.action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_click_config_provider(s_app_data.action_bar_layer, now_playing_click_config_provider);
  
  // Create clock text layer at the bottom
  s_app_data.clock_layer = text_layer_create(GRect(10, bounds.size.h - 30, bounds.size.w - ACTION_BAR_WIDTH - 20, 25));
  text_layer_set_text_alignment(s_app_data.clock_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.clock_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(s_app_data.clock_layer, GColorBlack);
  text_layer_set_background_color(s_app_data.clock_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.clock_layer));
  
  // Set press animations for better user feedback
  action_bar_layer_set_icon_press_animation(s_app_data.action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_app_data.action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_app_data.action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationMoveLeft);
  
  // Long press icons will be set dynamically in the long press handler
  
  action_bar_layer_add_to_window(s_app_data.action_bar_layer, window);
  
  // Create text layers for track info (ActionBarLayer takes up right side)
  // Create text layers with dynamic height calculation
  GFont track_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont artist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  
  // Calculate dynamic heights based on text content
  int track_height = calculate_text_height(s_app_data.track_name, track_font, bounds.size.w - ACTION_BAR_WIDTH - 20);
  int artist_height = calculate_text_height(s_app_data.artist_name, artist_font, bounds.size.w - ACTION_BAR_WIDTH - 20);
  
  // Ensure minimum heights
  if (track_height < 30) track_height = 30;
  if (artist_height < 25) artist_height = 25;
  
  s_app_data.track_layer = text_layer_create(GRect(10, 5, bounds.size.w - ACTION_BAR_WIDTH - 20, track_height));
  text_layer_set_text_alignment(s_app_data.track_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.track_layer, track_font);
  text_layer_set_text_color(s_app_data.track_layer, GColorBlack);
  text_layer_set_background_color(s_app_data.track_layer, GColorClear);
  text_layer_set_overflow_mode(s_app_data.track_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.track_layer));
  
  s_app_data.artist_layer = text_layer_create(GRect(10, 10 + track_height, bounds.size.w - ACTION_BAR_WIDTH - 20, artist_height));
  text_layer_set_text_alignment(s_app_data.artist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.artist_layer, artist_font);
  text_layer_set_text_color(s_app_data.artist_layer, GColorBlack);
  text_layer_set_background_color(s_app_data.artist_layer, GColorClear);
  text_layer_set_overflow_mode(s_app_data.artist_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.artist_layer));
  
  // Initialize now playing data
  s_app_data.is_active_session = false;
  strcpy(s_app_data.track_name, "No active session");
  strcpy(s_app_data.artist_name, "");
  s_app_data.is_playing = false;
  s_app_data.volume_percent = 50;
  s_app_data.can_skip_prev = true;
  s_app_data.can_skip_next = true;
  
  // Set background color
  window_set_background_color(window, GColorWhite);
  
  // Click config is handled by ActionBarLayer
  
  // Update display
  now_playing_update_display();
  
  // Start clock timer (update every minute)
  update_clock();
  s_app_data.clock_timer = app_timer_register(60000, (AppTimerCallback)update_clock, NULL);
  
  // Refresh current track data
  if (s_app_data.is_authenticated) {
    refresh_now_playing();
    
    // Set up periodic refresh every 10 seconds
    s_app_data.refresh_timer = app_timer_register(10000, (AppTimerCallback)refresh_now_playing, NULL);
  }
}

static void now_playing_window_unload(Window *window) {
  // Clean up timer
  if (s_app_data.refresh_timer) {
    app_timer_cancel(s_app_data.refresh_timer);
    s_app_data.refresh_timer = NULL;
  }
  
  // Clean up layers
  if (s_app_data.track_layer) {
    text_layer_destroy(s_app_data.track_layer);
    s_app_data.track_layer = NULL;
  }
  if (s_app_data.artist_layer) {
    text_layer_destroy(s_app_data.artist_layer);
    s_app_data.artist_layer = NULL;
  }
  if (s_app_data.clock_layer) {
    text_layer_destroy(s_app_data.clock_layer);
    s_app_data.clock_layer = NULL;
  }
  if (s_app_data.action_bar_layer) {
    action_bar_layer_destroy(s_app_data.action_bar_layer);
    s_app_data.action_bar_layer = NULL;
  }
  if (s_app_data.clock_timer) {
    app_timer_cancel(s_app_data.clock_timer);
    s_app_data.clock_timer = NULL;
  }
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
}

static void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  
  // Long press actions for track navigation
  if (!s_app_data.is_active_session) {
    return; // No action for long press when no active session
  }
  
  // Set long press icons before performing action
  if (s_app_data.action_bar_layer) {
    switch (button) {
      case BUTTON_ID_UP:
        // Show skip backward icon for long press
        if (s_app_data.can_skip_prev) {
          GBitmap *backward_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_BACKWARD);
          if (backward_bitmap) {
            action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_UP, backward_bitmap);
          }
        }
        break;
      case BUTTON_ID_DOWN:
        // Show skip forward icon for long press
        if (s_app_data.can_skip_next) {
          GBitmap *forward_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_FORWARD);
          if (forward_bitmap) {
            action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_DOWN, forward_bitmap);
          }
        }
        break;
      default:
        break;
    }
  }
  
  // Perform the track navigation action immediately
  switch (button) {
    case BUTTON_ID_UP:
      // Previous track (long press)
      if (s_app_data.can_skip_prev) {
        skip_to_previous();
      }
      break;
    case BUTTON_ID_DOWN:
      // Next track (long press)
      if (s_app_data.can_skip_next) {
        skip_to_next();
      }
      break;
    default:
      break;
  }
  
  // Restore original icons after a short delay to show the action was performed
  app_timer_register(500, (AppTimerCallback)now_playing_update_display, NULL);
}

static void now_playing_click_config_provider(void *context) {
  // Single clicks for volume and play/pause
  window_single_click_subscribe(BUTTON_ID_UP, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, now_playing_click_handler);
  
  // Long clicks for track navigation
  window_long_click_subscribe(BUTTON_ID_UP, 300, now_playing_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 300, now_playing_long_click_handler, NULL);
}


static void now_playing_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  
  // Single click actions for volume and play/pause
  now_playing_handle_action(button);
}


static void now_playing_handle_action(ButtonId button) {
  if (!s_app_data.is_active_session) {
    // Show helpful message when no active session
    strcpy(s_app_data.track_name, "No Active Session");
    strcpy(s_app_data.artist_name, "Start playing music on Spotify");
    now_playing_update_display();
    return;
  }
  
  // Ensure action bar icons are in correct state before handling action
  now_playing_update_display();
  
  // New simplified button behavior:
  // UP - Volume Up
  // SELECT - Play/Pause  
  // DOWN - Volume Down
  switch (button) {
    case BUTTON_ID_UP:
      // Volume up
      request_volume_change(BUTTON_ID_UP, 5);
      break;
    case BUTTON_ID_SELECT:
      // Play/pause
      play_pause_track();
      // Ensure icons are updated after play/pause state change
      now_playing_update_display();
      break;
    case BUTTON_ID_DOWN:
      // Volume down
      request_volume_change(BUTTON_ID_DOWN, -5);
      break;
    default:
      break;
  }
}


static void now_playing_update_display() {
  if (!s_app_data.now_playing_window) {
    return;
  }
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Updating display - track: '%s', artist: '%s', active: %s", 
  //         s_app_data.track_name, s_app_data.artist_name, s_app_data.is_active_session ? "true" : "false");
  
  // Determine the text content for each layer
  const char *track_text;
  const char *artist_text;
  
  if (!s_app_data.is_active_session) {
    track_text = "No Active Session";
    artist_text = "Start playing music on Spotify";
  } else {
    track_text = s_app_data.track_name;
    artist_text = s_app_data.artist_name;
  }
  
  // Get window bounds for width calculation
  Layer *window_layer = window_get_root_layer(s_app_data.now_playing_window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Calculate dynamic heights for the new text content
  GFont track_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont artist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  int track_height = calculate_text_height(track_text, track_font, bounds.size.w - ACTION_BAR_WIDTH - 20);
  int artist_height = calculate_text_height(artist_text, artist_font, bounds.size.w - ACTION_BAR_WIDTH - 20);

  // Ensure minimum heights
  if (track_height < 30) track_height = 30;
  if (artist_height < 25) artist_height = 25;

  // Allow artist to move further down if track name is very long
  int max_track_height = bounds.size.h - 80; // Reserve space for artist + clock + margins
  if (track_height > max_track_height) {
    track_height = max_track_height;
  }
  
  // Dynamic height limits based on screen size
  int max_artist_height = bounds.size.h - track_height - 20; // Reserve space for margins
  
  if (track_height > max_track_height) {
    track_height = max_track_height;
    // Switch to ellipsis mode for track text
    text_layer_set_overflow_mode(s_app_data.track_layer, GTextOverflowModeFill);
  } else {
    // Keep word wrap mode for track text
    text_layer_set_overflow_mode(s_app_data.track_layer, GTextOverflowModeWordWrap);
  }
  
  if (artist_height > max_artist_height) {
    artist_height = max_artist_height;
    // Switch to ellipsis mode for artist text
    text_layer_set_overflow_mode(s_app_data.artist_layer, GTextOverflowModeFill);
  } else {
    // Keep word wrap mode for artist text
    text_layer_set_overflow_mode(s_app_data.artist_layer, GTextOverflowModeWordWrap);
  }
  
  // Resize and reposition text layers
  if (s_app_data.track_layer) {
    layer_set_frame(text_layer_get_layer(s_app_data.track_layer), GRect(10, 5, bounds.size.w - ACTION_BAR_WIDTH - 20, track_height));
    text_layer_set_text(s_app_data.track_layer, track_text);
  }

  if (s_app_data.artist_layer) {
    layer_set_frame(text_layer_get_layer(s_app_data.artist_layer), GRect(10, 10 + track_height, bounds.size.w - ACTION_BAR_WIDTH - 20, artist_height));
    text_layer_set_text(s_app_data.artist_layer, artist_text);
  }
  
  // Update ActionBarLayer with new simplified button behavior
  // Skip updating action bar icons if we're showing a volume error
  if (s_app_data.action_bar_layer && !s_app_data.showing_volume_error) {
    if (!s_app_data.is_active_session) {
      // No active session - clear all icons
      action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_UP, NULL);
      action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_SELECT, NULL);
      action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_DOWN, NULL);
    } else {
      // New simplified behavior: Volume Up, Play/Pause, Volume Down (no animation on set)
      GBitmap *vol_up_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_UP);
      GBitmap *play_pause_bitmap = gbitmap_create_with_resource(s_app_data.is_playing ? 
        RESOURCE_ID_IMAGE_MUSIC_ICON_PAUSE : RESOURCE_ID_IMAGE_MUSIC_ICON_PLAY);
      GBitmap *vol_down_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_DOWN);
      
      if (vol_up_bitmap) action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_UP, vol_up_bitmap);
      if (play_pause_bitmap) action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_SELECT, play_pause_bitmap);
      if (vol_down_bitmap) action_bar_layer_set_icon(s_app_data.action_bar_layer, BUTTON_ID_DOWN, vol_down_bitmap);
    }
  }
}


// Authentication window implementation
static void auth_window_load(Window *window) {
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

static void auth_window_unload(Window *window) {
  // Clean up if needed
}

static void auth_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, auth_click_handler);
}

static void auth_click_handler(ClickRecognizerRef recognizer, void *context) {
  request_authentication();
}

static void request_authentication(void) {
  s_app_data.auth_state = AUTH_STATE_REQUESTING;
  
  // Send authentication request to JavaScript companion
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  // Send message_type as a string key with integer value
  dict_write_uint8(iter, 0, 0); // AUTH_REQUEST = 0
  
  app_message_outbox_send();
}

static void handle_auth_success(DictionaryIterator *iter) {
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
  save_auth_data();
  
  // Simple approach: just pop the auth window (main menu is already underneath)
  // APP_LOG(APP_LOG_LEVEL_INFO, "Authentication successful, popping auth window");
  
  if (s_app_data.auth_window) {
    window_stack_pop(true);
    window_destroy(s_app_data.auth_window);
    s_app_data.auth_window = NULL;
    // APP_LOG(APP_LOG_LEVEL_INFO, "Auth window popped and destroyed, main menu now visible");
  }
}


static void handle_auth_error(DictionaryIterator *iter) {
  s_app_data.auth_state = AUTH_STATE_ERROR;
  
  Tuple *error_tuple = dict_find(iter, 1);
  if (error_tuple) {
    strncpy(s_app_data.auth_error, error_tuple->value->cstring, sizeof(s_app_data.auth_error) - 1);
  }
  
  // TODO: Show error message to user
}

static void handle_api_response(DictionaryIterator *iter) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Received API response");
  
  // Get parsed data from JavaScript
  Tuple *track_name_tuple = dict_find(iter, 15); // TRACK_NAME key
  Tuple *artist_name_tuple = dict_find(iter, 16); // ARTIST_NAME key
  Tuple *is_playing_tuple = dict_find(iter, 17); // IS_PLAYING key
  Tuple *volume_tuple = dict_find(iter, 18); // VOLUME_PERCENT key
  Tuple *can_skip_prev_tuple = dict_find(iter, 19); // CAN_SKIP_PREV key
  Tuple *can_skip_next_tuple = dict_find(iter, 20); // CAN_SKIP_NEXT key
  
  // Update track name
  if (track_name_tuple) {
    strncpy(s_app_data.track_name, track_name_tuple->value->cstring, sizeof(s_app_data.track_name) - 1);
    s_app_data.track_name[sizeof(s_app_data.track_name) - 1] = '\0';
    // APP_LOG(APP_LOG_LEVEL_INFO, "Track name: %s", s_app_data.track_name);
  }
  
  // Update artist name
  if (artist_name_tuple) {
    strncpy(s_app_data.artist_name, artist_name_tuple->value->cstring, sizeof(s_app_data.artist_name) - 1);
    s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
    // APP_LOG(APP_LOG_LEVEL_INFO, "Artist name: %s", s_app_data.artist_name);
  }
  
  // Update playing status
  if (is_playing_tuple) {
    s_app_data.is_playing = (is_playing_tuple->value->uint8 == 1);
    // APP_LOG(APP_LOG_LEVEL_INFO, "Is playing: %s", s_app_data.is_playing ? "true" : "false");
  }
  
  // Update volume with stale data detection
  if (volume_tuple) {
    int api_volume = volume_tuple->value->uint8;
    
    // Detect stale data: if API returns a volume that's significantly different from our last known volume
    // and we have a pending volume change, it's likely stale data
    bool is_suspicious = false;
    if (s_app_data.volume_change_pending && s_app_data.last_known_volume >= 0) {
      int volume_diff = abs(api_volume - s_app_data.last_known_volume);
      // If the difference is more than 20% and we're not expecting such a big change, it's suspicious
      if (volume_diff > 20 && abs(s_app_data.pending_volume_delta) <= 20) {
        is_suspicious = true;
      }
      // Also detect the specific "50 bug" 
      if (api_volume == 50 && s_app_data.last_known_volume != 50) {
        is_suspicious = true;
      }
    }
    
    if (is_suspicious) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Detected stale volume data (%d), using local volume %d instead", api_volume, s_app_data.last_known_volume);
      // Keep current local volume, don't update from API
    } else {
      s_app_data.volume_percent = api_volume;
      s_app_data.last_known_volume = api_volume; // Update last known good volume
      APP_LOG(APP_LOG_LEVEL_INFO, "Volume from API: %d%%", s_app_data.volume_percent);
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
    apply_volume_change();
    return; // Skip normal display update since apply_volume_change handles it
  }
  
  // Update display if now playing window is active
  if (s_app_data.now_playing_window) {
    now_playing_update_display();
  }
}

static void handle_api_error(DictionaryIterator *iter) {
  Tuple *error_tuple = dict_find(iter, 13); // ERROR_MESSAGE key
  if (error_tuple) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "API Error: %s", error_tuple->value->cstring);
    
    // Check for specific error types
    if (strstr(error_tuple->value->cstring, "403")) {
      // Volume control failed - show X icon on the button that failed
      APP_LOG(APP_LOG_LEVEL_INFO, "Volume control failed (403), showing error for button %d", s_app_data.volume_error_button);
      show_volume_error(s_app_data.volume_error_button);
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

static void app_message_handler(DictionaryIterator *iter, void *context) {
  // Check for different message types by looking at the keys
  Tuple *auth_success_tuple = dict_find(iter, 1); // AUTH_SUCCESS
  Tuple *auth_error_tuple = dict_find(iter, 2);   // AUTH_ERROR
  Tuple *api_response_tuple = dict_find(iter, 5); // API_RESPONSE
  Tuple *api_error_tuple = dict_find(iter, 6);    // API_ERROR
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Message received - auth_success: %d, auth_error: %d, api_response: %d, api_error: %d", 
  //         auth_success_tuple ? 1 : 0, auth_error_tuple ? 1 : 0, api_response_tuple ? 1 : 0, api_error_tuple ? 1 : 0);
  
  if (auth_success_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Calling handle_auth_success");
    handle_auth_success(iter);
  } else if (auth_error_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Calling handle_auth_error");
    handle_auth_error(iter);
  } else if (api_response_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Handling API response");
    handle_api_response(iter);
  } else if (api_error_tuple) {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Handling API error");
    handle_api_error(iter);
  } else {
    // APP_LOG(APP_LOG_LEVEL_INFO, "Unknown message type received");
  }
}

static void app_message_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  // Handle outbox failure
  s_app_data.auth_state = AUTH_STATE_ERROR;
  strcpy(s_app_data.auth_error, "Failed to send message");
}

static void app_message_outbox_sent(DictionaryIterator *iter, void *context) {
  // Message sent successfully
}

// Spotify API call functions
static void make_spotify_api_call(const char *path, const char *method, const char *data) {
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

static void refresh_now_playing(void) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "refresh_now_playing called");
  make_spotify_api_call("/me/player", "GET", NULL);
  
  // Restart the periodic refresh timer if now playing window is active
  if (s_app_data.now_playing_window && s_app_data.is_authenticated) {
    s_app_data.refresh_timer = app_timer_register(10000, (AppTimerCallback)refresh_now_playing, NULL);
  }
}

static void play_pause_track(void) {
  const char *action = s_app_data.is_playing ? "pause" : "play";
  char path[64];
  snprintf(path, sizeof(path), "/me/player/%s", action);
  make_spotify_api_call(path, "PUT", NULL);
  // Refresh now playing data after play/pause
  app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
}

static void skip_to_next(void) {
  make_spotify_api_call("/me/player/next", "POST", NULL);
  // Refresh now playing data after skipping
  app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
}

static void skip_to_previous(void) {
  make_spotify_api_call("/me/player/previous", "POST", NULL);
  // Refresh now playing data after skipping
  app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
}

static void set_volume(int volume_percent, ButtonId button) {
  char path[128];
  snprintf(path, sizeof(path), "/me/player/volume?volume_percent=%d", volume_percent);
  // APP_LOG(APP_LOG_LEVEL_INFO, "Setting volume to %d%%", volume_percent);
  
  // Store which button was pressed for error handling
  s_app_data.volume_error_button = button;
  
  make_spotify_api_call(path, "PUT", NULL);
}

static int calculate_text_height(const char *text, GFont font, int width) {
  if (!text || strlen(text) == 0) {
    return 20; // Default height for empty text
  }

  // Use graphics_text_layout_get_content_size to calculate the height needed
  GRect bounds = GRect(0, 0, width, 200); // Large height to measure
  GSize text_size = graphics_text_layout_get_content_size(text, font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter);

  return text_size.h + 5; // Add small padding
}

static void show_volume_error(ButtonId button) {
  if (!s_app_data.action_bar_layer) {
    return;
  }
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Showing volume error for button %d", button);
  
  // Set flag to prevent display updates from overriding error icon
  s_app_data.showing_volume_error = true;
  
  // Show dismiss icon as error indicator
  GBitmap *error_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_DISMISS);
  if (error_bitmap) {
    action_bar_layer_set_icon(s_app_data.action_bar_layer, button, error_bitmap);
  } else {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Failed to load dismiss icon for volume error");
  }
  
  // Clear the error after 1 second
  s_app_data.volume_error_timer = app_timer_register(1000, (AppTimerCallback)clear_volume_error, NULL);
}

static void clear_volume_error(void) {
  // Clear the error flag
  s_app_data.showing_volume_error = false;
  
  // Restore normal display
  now_playing_update_display();
  s_app_data.volume_error_timer = NULL;
}

static void update_clock(void) {
  if (!s_app_data.clock_layer) {
    return;
  }

  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  
  static char clock_text[16];
  strftime(clock_text, sizeof(clock_text), "%H:%M", tick_time);
  
  text_layer_set_text(s_app_data.clock_layer, clock_text);
  
  // Restart timer for next update (every minute)
  s_app_data.clock_timer = app_timer_register(60000, (AppTimerCallback)update_clock, NULL);
}

static void request_volume_change(ButtonId button, int delta) {
  // If there's already a volume change pending, accumulate the delta locally
  if (s_app_data.volume_change_pending) {
    s_app_data.pending_volume_delta += delta;
    APP_LOG(APP_LOG_LEVEL_INFO, "Accumulating volume change: delta=%d, total=%d", delta, s_app_data.pending_volume_delta);
    return;
  }
  
  // Start a new volume change request
  s_app_data.volume_change_pending = true;
  s_app_data.pending_volume_button = button;
  s_app_data.pending_volume_delta = delta;
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Requesting volume change: button=%d, delta=%d", button, delta);
  
  // Only fetch current volume if we haven't fetched it recently (within last 2 seconds)
  time_t current_time = time(NULL);
  if (current_time - s_app_data.last_volume_fetch_time > 2) {
    s_app_data.last_volume_fetch_time = current_time;
    make_spotify_api_call("/me/player", "GET", NULL);
  } else {
    // Use current local volume and apply change immediately
    APP_LOG(APP_LOG_LEVEL_INFO, "Using local volume (recent fetch), applying change immediately");
    apply_volume_change();
  }
}

static void apply_volume_change(void) {
  if (!s_app_data.volume_change_pending) {
    return;
  }
  
  // Calculate new volume based on current API volume + accumulated delta
  int new_volume = s_app_data.volume_percent + s_app_data.pending_volume_delta;
  
  // Clamp to valid range
  if (new_volume < 0) new_volume = 0;
  if (new_volume > 100) new_volume = 100;
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Applying volume change: current=%d, delta=%d, new=%d", 
          s_app_data.volume_percent, s_app_data.pending_volume_delta, new_volume);
  
  // Update local volume immediately for UI feedback
  s_app_data.volume_percent = new_volume;
  
  // Update display immediately to show new volume
  now_playing_update_display();
  
  // Send volume change to API
  set_volume(new_volume, s_app_data.pending_volume_button);
  
  // Clear pending state
  s_app_data.volume_change_pending = false;
  s_app_data.pending_volume_delta = 0;
  
  // Refresh now playing data after volume change
  app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
}


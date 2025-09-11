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
typedef enum {
  CONTROL_MODE_DEFAULT,
  CONTROL_MODE_VOLUME,
  CONTROL_MODE_TOOLS
} ControlMode;

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
  ControlMode control_mode;
  char track_name[128];
  char artist_name[128];
  bool is_playing;
  int volume_percent;
  bool can_skip_prev;
  bool can_skip_next;
  
  // Now playing UI elements
  TextLayer *track_layer;
  TextLayer *artist_layer;
  TextLayer *status_layer;
  Layer *display_layer;
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
static void set_volume(int volume_percent);

// Now playing window functions
static void now_playing_window_load(Window *window);
static void now_playing_window_unload(Window *window);
static void now_playing_click_handler(ClickRecognizerRef recognizer, void *context);
static void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *data);
static void now_playing_click_config_provider(void *context);
static void now_playing_update_display(void);
static void now_playing_set_control_mode(ControlMode mode);
static void now_playing_handle_action(ButtonId button);
static void now_playing_display_update_proc(Layer *layer, GContext *ctx);

int main(void) {
  init_app();
  app_event_loop();
  deinit_app();
}

static void init_app(void) {
  // Initialize app data
  memset(&s_app_data, 0, sizeof(AppData));
  s_app_data.current_state = APP_STATE_AUTH_REQUIRED;
  s_app_data.auth_state = AUTH_STATE_NONE;
  
  // Set up AppMessage
  app_message_register_inbox_received(app_message_handler);
  app_message_register_outbox_failed(app_message_outbox_failed);
  app_message_register_outbox_sent(app_message_outbox_sent);
  
  const uint32_t inbox_size = 1024;
  const uint32_t outbox_size = 1024;
  app_message_open(inbox_size, outbox_size);
  
  // Check if we have stored authentication tokens
  // For now, assume we need authentication
  s_app_data.auth_state = AUTH_STATE_NONE;
  
  if (s_app_data.auth_state == AUTH_STATE_NONE) {
    // Show authentication window
    s_app_data.auth_window = window_create();
    window_set_window_handlers(s_app_data.auth_window, (WindowHandlers) {
      .load = auth_window_load,
      .unload = auth_window_unload,
    });
    window_stack_push(s_app_data.auth_window, true);
  } else {
    // Show main menu
    s_app_data.current_state = APP_STATE_MAIN_MENU;
    s_app_data.main_window = window_create();
    window_set_window_handlers(s_app_data.main_window, (WindowHandlers) {
      .load = main_window_load,
      .unload = main_window_unload,
    });
    window_stack_push(s_app_data.main_window, true);
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
  
  // Set up click recognizer
  window_set_click_config_provider(window, now_playing_click_config_provider);
  
  // Create display layer
  s_app_data.display_layer = layer_create(bounds);
  layer_set_update_proc(s_app_data.display_layer, now_playing_display_update_proc);
  layer_add_child(window_layer, s_app_data.display_layer);
  
  // Create text layers
  s_app_data.track_layer = text_layer_create(GRect(10, 20, bounds.size.w - 20, 30));
  text_layer_set_text_alignment(s_app_data.track_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.track_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(s_app_data.track_layer, GColorWhite);
  text_layer_set_background_color(s_app_data.track_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.track_layer));
  
  s_app_data.artist_layer = text_layer_create(GRect(10, 50, bounds.size.w - 20, 25));
  text_layer_set_text_alignment(s_app_data.artist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.artist_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(s_app_data.artist_layer, GColorWhite);
  text_layer_set_background_color(s_app_data.artist_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.artist_layer));
  
  s_app_data.status_layer = text_layer_create(GRect(10, 120, bounds.size.w - 20, 30));
  text_layer_set_text_alignment(s_app_data.status_layer, GTextAlignmentCenter);
  text_layer_set_font(s_app_data.status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_color(s_app_data.status_layer, GColorWhite);
  text_layer_set_background_color(s_app_data.status_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_app_data.status_layer));
  
  // Initialize now playing data
  s_app_data.is_active_session = false;
  s_app_data.control_mode = CONTROL_MODE_DEFAULT;
  strcpy(s_app_data.track_name, "No active session");
  strcpy(s_app_data.artist_name, "");
  s_app_data.is_playing = false;
  s_app_data.volume_percent = 50;
  s_app_data.can_skip_prev = true;
  s_app_data.can_skip_next = true;
  
  // Update display
  now_playing_update_display();
  
  // Refresh current track data
  if (s_app_data.is_authenticated) {
    refresh_now_playing();
  }
}

static void now_playing_window_unload(Window *window) {
  // Clean up layers
  if (s_app_data.track_layer) {
    text_layer_destroy(s_app_data.track_layer);
    s_app_data.track_layer = NULL;
  }
  if (s_app_data.artist_layer) {
    text_layer_destroy(s_app_data.artist_layer);
    s_app_data.artist_layer = NULL;
  }
  if (s_app_data.status_layer) {
    text_layer_destroy(s_app_data.status_layer);
    s_app_data.status_layer = NULL;
  }
  if (s_app_data.display_layer) {
    layer_destroy(s_app_data.display_layer);
    s_app_data.display_layer = NULL;
  }
}

static void now_playing_click_config_provider(void *context) {
  // Single click handler
  window_single_click_subscribe(BUTTON_ID_UP, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, now_playing_click_handler);
  
  // Long click handler for tools mode
  window_long_click_subscribe(BUTTON_ID_SELECT, 1000, now_playing_long_click_handler, NULL);
}

static void now_playing_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  now_playing_handle_action(button);
}

static void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *data) {
  if (s_app_data.is_active_session && s_app_data.control_mode == CONTROL_MODE_DEFAULT) {
    now_playing_set_control_mode(CONTROL_MODE_TOOLS);
  }
}

static void now_playing_handle_action(ButtonId button) {
  if (!s_app_data.is_active_session) {
    return;
  }
  
  switch (s_app_data.control_mode) {
    case CONTROL_MODE_DEFAULT:
      switch (button) {
        case BUTTON_ID_UP:
          // Previous track
          if (s_app_data.can_skip_prev) {
            skip_to_previous();
          }
          break;
        case BUTTON_ID_SELECT:
          // Switch to volume mode
          now_playing_set_control_mode(CONTROL_MODE_VOLUME);
          break;
        case BUTTON_ID_DOWN:
          // Next track
          if (s_app_data.can_skip_next) {
            skip_to_next();
          }
          break;
        default:
          break;
      }
      break;
      
    case CONTROL_MODE_VOLUME:
      switch (button) {
        case BUTTON_ID_UP:
          // Volume up
          s_app_data.volume_percent = (s_app_data.volume_percent + 10 > 100) ? 100 : s_app_data.volume_percent + 10;
          set_volume(s_app_data.volume_percent);
          // Refresh now playing data after volume change
          app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
          break;
        case BUTTON_ID_SELECT:
          // Play/pause
          play_pause_track();
          break;
        case BUTTON_ID_DOWN:
          // Volume down
          s_app_data.volume_percent = (s_app_data.volume_percent - 10 < 0) ? 0 : s_app_data.volume_percent - 10;
          set_volume(s_app_data.volume_percent);
          // Refresh now playing data after volume change
          app_timer_register(500, (AppTimerCallback)refresh_now_playing, NULL);
          break;
        default:
          break;
      }
      // Auto-return to default mode after 2 seconds
      app_timer_register(2000, (AppTimerCallback)now_playing_set_control_mode, (void*)CONTROL_MODE_DEFAULT);
      break;
      
    case CONTROL_MODE_TOOLS:
      switch (button) {
        case BUTTON_ID_UP:
          // Shuffle
          // TODO: Make API call to toggle shuffle
          break;
        case BUTTON_ID_SELECT:
          // Favorite
          // TODO: Make API call to toggle favorite
          break;
        case BUTTON_ID_DOWN:
          // More options
          // TODO: Show more options
          break;
        default:
          break;
      }
      // Auto-return to default mode after 2 seconds
      app_timer_register(2000, (AppTimerCallback)now_playing_set_control_mode, (void*)CONTROL_MODE_DEFAULT);
      break;
  }
  
  now_playing_update_display();
}

static void now_playing_set_control_mode(ControlMode mode) {
  s_app_data.control_mode = mode;
  now_playing_update_display();
}

static void now_playing_update_display() {
  if (!s_app_data.now_playing_window) {
    return;
  }
  
  // Update text layers
  if (s_app_data.track_layer) {
    text_layer_set_text(s_app_data.track_layer, s_app_data.track_name);
  }
  if (s_app_data.artist_layer) {
    text_layer_set_text(s_app_data.artist_layer, s_app_data.artist_name);
  }
  
  // Update status based on control mode
  char status_text[64];
  switch (s_app_data.control_mode) {
    case CONTROL_MODE_DEFAULT:
      snprintf(status_text, sizeof(status_text), "UP:Prev SEL:Vol DOWN:Next");
      break;
    case CONTROL_MODE_VOLUME:
      snprintf(status_text, sizeof(status_text), "Vol: %d%% SEL:%s", 
               s_app_data.volume_percent, s_app_data.is_playing ? "Pause" : "Play");
      break;
    case CONTROL_MODE_TOOLS:
      snprintf(status_text, sizeof(status_text), "UP:Shuffle SEL:Fav DOWN:More");
      break;
  }
  
  if (s_app_data.status_layer) {
    text_layer_set_text(s_app_data.status_layer, status_text);
  }
  
  // Mark display layer for redraw
  if (s_app_data.display_layer) {
    layer_mark_dirty(s_app_data.display_layer);
  }
}

static void now_playing_display_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  
  // Set background color (Spotify green)
  graphics_context_set_fill_color(ctx, GColorJaegerGreen);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
  
  // Draw control icons based on mode
  int icon_size = 20;
  int icon_y = bounds.size.h - 40;
  
  switch (s_app_data.control_mode) {
    case CONTROL_MODE_DEFAULT:
      // Draw previous, play/pause, next icons
      if (s_app_data.can_skip_prev) {
        graphics_draw_bitmap_in_rect(ctx, 
          gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_BACKWARD),
          GRect(20, icon_y, icon_size, icon_size));
      }
      
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(s_app_data.is_playing ? 
          RESOURCE_ID_IMAGE_MUSIC_ICON_PAUSE : RESOURCE_ID_IMAGE_MUSIC_ICON_PLAY),
        GRect(bounds.size.w/2 - icon_size/2, icon_y, icon_size, icon_size));
      
      if (s_app_data.can_skip_next) {
        graphics_draw_bitmap_in_rect(ctx, 
          gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_FORWARD),
          GRect(bounds.size.w - 40, icon_y, icon_size, icon_size));
      }
      break;
      
    case CONTROL_MODE_VOLUME:
      // Draw volume up, play/pause, volume down icons
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_UP),
        GRect(20, icon_y, icon_size, icon_size));
      
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(s_app_data.is_playing ? 
          RESOURCE_ID_IMAGE_MUSIC_ICON_PAUSE : RESOURCE_ID_IMAGE_MUSIC_ICON_PLAY),
        GRect(bounds.size.w/2 - icon_size/2, icon_y, icon_size, icon_size));
      
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_DOWN),
        GRect(bounds.size.w - 40, icon_y, icon_size, icon_size));
      break;
      
    case CONTROL_MODE_TOOLS:
      // Draw shuffle, favorite, more icons
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_SHUFFLE),
        GRect(20, icon_y, icon_size, icon_size));
      
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_FAVORITE),
        GRect(bounds.size.w/2 - icon_size/2, icon_y, icon_size, icon_size));
      
      graphics_draw_bitmap_in_rect(ctx, 
        gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MUSIC_ICON_ELLIPSIS),
        GRect(bounds.size.w - 40, icon_y, icon_size, icon_size));
      break;
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
  
  // Switch to main menu
  window_stack_pop_all(true);
  s_app_data.current_state = APP_STATE_MAIN_MENU;
  s_app_data.main_window = window_create();
  window_set_window_handlers(s_app_data.main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_app_data.main_window, true);
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
  APP_LOG(APP_LOG_LEVEL_INFO, "Received API response");
  
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
    APP_LOG(APP_LOG_LEVEL_INFO, "Track name: %s", s_app_data.track_name);
  }
  
  // Update artist name
  if (artist_name_tuple) {
    strncpy(s_app_data.artist_name, artist_name_tuple->value->cstring, sizeof(s_app_data.artist_name) - 1);
    s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
    APP_LOG(APP_LOG_LEVEL_INFO, "Artist name: %s", s_app_data.artist_name);
  }
  
  // Update playing status
  if (is_playing_tuple) {
    s_app_data.is_playing = (is_playing_tuple->value->uint8 == 1);
    APP_LOG(APP_LOG_LEVEL_INFO, "Is playing: %s", s_app_data.is_playing ? "true" : "false");
  }
  
  // Update volume
  if (volume_tuple) {
    s_app_data.volume_percent = volume_tuple->value->uint8;
    APP_LOG(APP_LOG_LEVEL_INFO, "Volume: %d%%", s_app_data.volume_percent);
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

static void handle_api_error(DictionaryIterator *iter) {
  Tuple *error_tuple = dict_find(iter, 13); // ERROR_MESSAGE key
  if (error_tuple) {
    // Handle API error
    s_app_data.is_active_session = false;
    strcpy(s_app_data.track_name, "Error loading track");
    strcpy(s_app_data.artist_name, "");
    
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
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Message received - auth_success: %d, auth_error: %d, api_response: %d, api_error: %d", 
          auth_success_tuple ? 1 : 0, auth_error_tuple ? 1 : 0, api_response_tuple ? 1 : 0, api_error_tuple ? 1 : 0);
  
  if (auth_success_tuple) {
    handle_auth_success(iter);
  } else if (auth_error_tuple) {
    handle_auth_error(iter);
  } else if (api_response_tuple) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Handling API response");
    handle_api_response(iter);
  } else if (api_error_tuple) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Handling API error");
    handle_api_error(iter);
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
  APP_LOG(APP_LOG_LEVEL_INFO, "Making Spotify API call: %s %s", method, path);
  
  app_message_outbox_send();
}

static void refresh_now_playing(void) {
  APP_LOG(APP_LOG_LEVEL_INFO, "refresh_now_playing called");
  make_spotify_api_call("/me/player", "GET", NULL);
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

static void set_volume(int volume_percent) {
  char path[128];
  snprintf(path, sizeof(path), "/me/player/volume?volume_percent=%d", volume_percent);
  make_spotify_api_call(path, "PUT", NULL);
}


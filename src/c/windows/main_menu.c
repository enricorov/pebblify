#include "main_menu.h"
#include "now_playing.h"

// Forward declaration for now playing window creation
extern void now_playing_window_create(void);

void main_menu_init(void) {
  // Create main window
  s_app_data.main_window = window_create();
  window_set_window_handlers(s_app_data.main_window, (WindowHandlers) {
    .load = main_menu_load,
    .unload = main_menu_unload,
  });
  window_stack_push(s_app_data.main_window, true);
}

void main_menu_deinit(void) {
  if (s_app_data.main_window) {
    window_destroy(s_app_data.main_window);
    s_app_data.main_window = NULL;
  }
}

void main_menu_load(Window *window) {
  // APP_LOG(APP_LOG_LEVEL_INFO, "Main window load called");
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create main menu
  MenuLayer *main_menu = menu_layer_create(bounds);
  menu_layer_set_callbacks(main_menu, NULL, (MenuLayerCallbacks) {
    .get_num_sections = main_menu_get_num_sections_callback,
    .get_num_rows = main_menu_get_num_rows_callback,
    .get_header_height = main_menu_get_header_height_callback,
    .draw_header = main_menu_draw_header_callback,
    .draw_row = main_menu_draw_row_callback,
    .select_click = main_menu_select_callback,
  });
  
  menu_layer_set_click_config_onto_window(main_menu, window);
  
  // Enable clock in menu layer
  menu_layer_set_highlight_colors(main_menu, GColorBlack, GColorWhite);
  
  layer_add_child(window_layer, menu_layer_get_layer(main_menu));
}

void main_menu_unload(Window *window) {
  // Clean up menu layer - the menu layer will be automatically destroyed
  // when the window is destroyed, so we don't need to do anything here
}

uint16_t main_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return 3; // Home, Library, Devices
}

uint16_t main_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case 0: return 3; // Home: Now playing, Jump back in, Made for you
    case 1: return 3; // Library: Playlists, Albums, Artists
    case 2: return 1; // Devices: Play on device
    default: return 0;
  }
}

int16_t main_menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  return MENU_CELL_BASIC_HEADER_HEIGHT;
}

void main_menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data) {
  const char *headers[] = {"Home", "Library", "Devices"};
  menu_cell_basic_header_draw(ctx, cell_layer, headers[section_index]);
}

void main_menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data) {
  const char *items[3][3] = {
    {"Now playing", "Jump back in", "Made for you"},
    {"Playlists", "Albums", "Artists"},
    {"Play on device", "", ""}
  };
  
  menu_cell_basic_draw(ctx, cell_layer, items[cell_index->section][cell_index->row], NULL, NULL);
}

void main_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  // Handle menu selection
  if (cell_index->section == 0 && cell_index->row == 0) {
    // Now playing selected
    s_app_data.current_state = APP_STATE_NOW_PLAYING;
    now_playing_window_create();
  } else if (cell_index->section == 1 && cell_index->row == 0) {
    // Playlists selected
    s_app_data.current_state = APP_STATE_PLAYLISTS;
    // TODO: Show playlists window
  }
}

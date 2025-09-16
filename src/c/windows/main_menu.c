#include "main_menu.h"
#include "now_playing.h"
#include "../core/app_state.h"
#include "../core/constants.h"

// Forward declarations
extern void now_playing_window_create(void);
extern void click_config_provider(void *context);

void main_menu_init(void) {
  s_app_data.main_window = window_create();
  window_set_window_handlers(s_app_data.main_window, (WindowHandlers) {
    .load = main_menu_load,
    .unload = main_menu_unload,
  });
}

void main_menu_deinit(void) {
  if (s_app_data.main_window) {
    window_destroy(s_app_data.main_window);
    s_app_data.main_window = NULL;
  }
}

void main_menu_load(Window *window) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Main menu load called");
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
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
  
  menu_layer_set_highlight_colors(main_menu, 
    PBL_IF_COLOR_ELSE(GColorJaegerGreen, GColorBlack),
    PBL_IF_COLOR_ELSE(GColorWhite, GColorWhite));
  
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
  
  layer_add_child(window_layer, menu_layer_get_layer(main_menu));
}

void main_menu_unload(Window *window) {
  // MenuLayer is created locally in main_menu_load and should be cleaned up automatically
  // when the window is destroyed. No explicit cleanup needed for local variables.
}

uint16_t main_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data) {
  return MAIN_MENU_SECTIONS;
}

uint16_t main_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data) {
  switch (section_index) {
    case 0: return HOME_MENU_ROWS;
    case 1: return LIBRARY_MENU_ROWS;
    case 2: return DEVICES_MENU_ROWS;
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
    {"Jump back in", "Made for you", ""},
    {"Playlists", "Albums", "Artists"},
    {"Play on device", "", ""}
  };
  
  menu_cell_basic_draw(ctx, cell_layer, items[cell_index->section][cell_index->row], NULL, NULL);
}

void main_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data) {
  if (cell_index->section == 0 && cell_index->row == 0) {
    // TODO: Implement jump back in functionality
  } else if (cell_index->section == 0 && cell_index->row == 1) {
    // TODO: Implement made for you functionality
  } else if (cell_index->section == 1 && cell_index->row == 0) {
    s_app_data.current_state = APP_STATE_PLAYLISTS;
    // TODO: Show playlists window
  }
}

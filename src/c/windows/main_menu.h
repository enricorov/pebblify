#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <pebble.h>
#include "../core/app_state.h"

// Main menu functions
void main_menu_init(void);
void main_menu_deinit(void);
void main_menu_load(Window *window);
void main_menu_unload(Window *window);

// Menu callbacks
uint16_t main_menu_get_num_sections_callback(MenuLayer *menu_layer, void *data);
uint16_t main_menu_get_num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *data);
int16_t main_menu_get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *data);
void main_menu_draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *data);
void main_menu_draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *data);
void main_menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *data);

#endif // MAIN_MENU_H

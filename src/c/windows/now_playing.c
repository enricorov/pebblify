/**
 * Now Playing Window Module
 * 
 * Handles the now playing interface:
 * - Creates and manages the now playing window UI
 * - Handles button interactions and sends action commands to JavaScript
 * - Updates display based on track data received from JavaScript
 * - Provides visual feedback for volume errors
 * - Manages polling control via AppMessage to JavaScript
 */

#include "now_playing.h"
#include "../api/spotify_api.h"
#include "../core/app_state.h"
#include "../core/constants.h"

// UI Layer Management

static TextLayer *s_track_layer;
static TextLayer *s_artist_layer;
static TextLayer *s_clock_layer;
static ActionBarLayer *s_action_bar_layer;
static AppTimer *s_clock_timer;
static AppTimer *s_action_feedback_timer;

// Module Initialization and Cleanup

void now_playing_init(void) {
  s_app_data.is_active_session = false;
  strncpy(s_app_data.track_name, DEFAULT_NO_SESSION_TEXT, sizeof(s_app_data.track_name) - 1);
  s_app_data.track_name[sizeof(s_app_data.track_name) - 1] = '\0';
  strncpy(s_app_data.artist_name, "", sizeof(s_app_data.artist_name) - 1);
  s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
  s_app_data.is_playing = false;
  s_app_data.volume_percent = VOLUME_DEFAULT;
  s_app_data.can_skip_prev = true;
  s_app_data.can_skip_next = true;
}

void now_playing_deinit(void) {
  now_playing_window_destroy();
}

// Window Management

void now_playing_window_create(void) {
  if (s_app_data.now_playing_window) {
    window_stack_push(s_app_data.now_playing_window, true);
    return;
  }
  
  s_app_data.now_playing_window = window_create();
  window_set_window_handlers(s_app_data.now_playing_window, (WindowHandlers) {
    .load = now_playing_window_load,
    .unload = now_playing_window_unload,
  });
  
  window_stack_push(s_app_data.now_playing_window, true);
}

void now_playing_window_pop(void) {
  if (s_app_data.now_playing_window && window_stack_get_top_window() == s_app_data.now_playing_window) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, MESSAGE_KEY_STOP_POLLING, 1);
    app_message_outbox_send();
    
    if (s_clock_timer) {
      app_timer_cancel(s_clock_timer);
      s_clock_timer = NULL;
    }
    if (s_action_feedback_timer) {
      app_timer_cancel(s_action_feedback_timer);
      s_action_feedback_timer = NULL;
    }
    if (s_app_data.volume_error_timer) {
      app_timer_cancel(s_app_data.volume_error_timer);
      s_app_data.volume_error_timer = NULL;
    }
    
    window_stack_pop(true);
    s_app_data.current_state = APP_STATE_MAIN_MENU;
  }
}

void now_playing_window_destroy(void) {
  if (s_app_data.now_playing_window) {
    if (window_stack_get_top_window() == s_app_data.now_playing_window) {
      window_stack_pop(true);
    }
    
    window_destroy(s_app_data.now_playing_window);
    s_app_data.now_playing_window = NULL;
    s_app_data.current_state = APP_STATE_MAIN_MENU;
  }
}

// Window Lifecycle Handlers

void now_playing_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  s_action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_click_config_provider(s_action_bar_layer, now_playing_click_config_provider);
  
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationMoveLeft);
  
  action_bar_layer_add_to_window(s_action_bar_layer, window);
  
  s_clock_layer = text_layer_create(GRect(MARGIN_MEDIUM, bounds.size.h - CLOCK_HEIGHT - MARGIN_SMALL, 
                                          bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, CLOCK_HEIGHT));
  text_layer_set_text_alignment(s_clock_layer, GTextAlignmentCenter);
  text_layer_set_font(s_clock_layer, fonts_get_system_font(FONT_KEY_CLOCK));
  text_layer_set_text_color(s_clock_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  text_layer_set_background_color(s_clock_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_clock_layer));
  
  GFont track_font = fonts_get_system_font(FONT_KEY_TRACK);
  GFont artist_font = fonts_get_system_font(FONT_KEY_ARTIST);
  
  int track_height = now_playing_calculate_text_height(s_app_data.track_name, track_font, 
                                                      bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE);
  int artist_height = now_playing_calculate_text_height(s_app_data.artist_name, artist_font, 
                                                       bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE);
  
  if (track_height < MIN_TRACK_HEIGHT) track_height = MIN_TRACK_HEIGHT;
  if (artist_height < MIN_ARTIST_HEIGHT) artist_height = MIN_ARTIST_HEIGHT;
  
  s_track_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_SMALL, 
                                          bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, track_height));
  text_layer_set_text_alignment(s_track_layer, GTextAlignmentCenter);
  text_layer_set_font(s_track_layer, track_font);
  text_layer_set_text_color(s_track_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  text_layer_set_background_color(s_track_layer, GColorClear);
  text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_track_layer));
  
  s_artist_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_MEDIUM + track_height, 
                                           bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, artist_height));
  text_layer_set_text_alignment(s_artist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_artist_layer, artist_font);
  text_layer_set_text_color(s_artist_layer, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  text_layer_set_background_color(s_artist_layer, GColorClear);
  text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_artist_layer));
  
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
  
  now_playing_update_display();
  now_playing_update_clock();
  
  if (s_app_data.is_authenticated) {
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_uint8(iter, MESSAGE_KEY_START_POLLING, 1);
    app_message_outbox_send();
  }
}

void now_playing_window_unload(Window *window) {
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint8(iter, MESSAGE_KEY_STOP_POLLING, 1);
  app_message_outbox_send();
  
  if (s_clock_timer) {
    app_timer_cancel(s_clock_timer);
    s_clock_timer = NULL;
  }
  if (s_action_feedback_timer) {
    app_timer_cancel(s_action_feedback_timer);
    s_action_feedback_timer = NULL;
  }
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
  
  if (s_track_layer) {
    text_layer_destroy(s_track_layer);
    s_track_layer = NULL;
  }
  if (s_artist_layer) {
    text_layer_destroy(s_artist_layer);
    s_artist_layer = NULL;
  }
  if (s_clock_layer) {
    text_layer_destroy(s_clock_layer);
    s_clock_layer = NULL;
  }
  if (s_action_bar_layer) {
    action_bar_layer_destroy(s_action_bar_layer);
    s_action_bar_layer = NULL;
  }
}

// Button Interaction Handlers

void now_playing_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, now_playing_click_handler);
  
  window_long_click_subscribe(BUTTON_ID_UP, LONG_CLICK_DURATION_MS, now_playing_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_SELECT, LONG_CLICK_DURATION_MS, now_playing_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, LONG_CLICK_DURATION_MS, now_playing_long_click_handler, NULL);
}

void now_playing_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  now_playing_handle_action(button);
}

void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  
  if (button == BUTTON_ID_SELECT) {
    s_app_data.current_state = APP_STATE_MAIN_MENU;
    if (s_app_data.main_window) {
      window_stack_push(s_app_data.main_window, true);
    }
    return;
  }
  
  if (!s_app_data.is_active_session) {
    return;
  }
  
  if (s_action_bar_layer) {
    switch (button) {
      case BUTTON_ID_UP:
        if (s_app_data.can_skip_prev) {
          GBitmap *backward_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_backward_bitmap, 
                                                                RESOURCE_ID_IMAGE_MUSIC_ICON_BACKWARD);
          if (backward_bitmap) {
            action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, backward_bitmap);
          }
        }
        break;
      case BUTTON_ID_DOWN:
        if (s_app_data.can_skip_next) {
          GBitmap *forward_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_forward_bitmap, 
                                                               RESOURCE_ID_IMAGE_MUSIC_ICON_FORWARD);
          if (forward_bitmap) {
            action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, forward_bitmap);
          }
        }
        break;
      default:
        break;
    }
  }
  
  switch (button) {
    case BUTTON_ID_UP:
      if (s_app_data.can_skip_prev) {
        spotify_api_skip_to_previous();
      }
      break;
    case BUTTON_ID_DOWN:
      if (s_app_data.can_skip_next) {
        spotify_api_skip_to_next();
      }
      break;
    default:
      break;
  }
  
  if (s_app_data.now_playing_window && window_stack_get_top_window() == s_app_data.now_playing_window && !s_action_feedback_timer) {
    s_action_feedback_timer = app_timer_register(ACTION_FEEDBACK_DELAY_MS, (AppTimerCallback)now_playing_update_display, NULL);
  }
}

void now_playing_handle_action(ButtonId button) {
  if (!s_app_data.is_active_session) {
    strncpy(s_app_data.track_name, DEFAULT_NO_ACTIVE_SESSION_TEXT, sizeof(s_app_data.track_name) - 1);
    s_app_data.track_name[sizeof(s_app_data.track_name) - 1] = '\0';
    strncpy(s_app_data.artist_name, DEFAULT_START_MUSIC_TEXT, sizeof(s_app_data.artist_name) - 1);
    s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
    now_playing_update_display();
    return;
  }
  
  now_playing_update_display();
  
  switch (button) {
    case BUTTON_ID_UP:
      spotify_api_volume_up();
      break;
    case BUTTON_ID_SELECT:
      spotify_api_play_pause_track();
      break;
    case BUTTON_ID_DOWN:
      spotify_api_volume_down();
      break;
    default:
      break;
  }
}

// Display Management

void now_playing_update_display(void) {
  if (!s_app_data.now_playing_window || window_stack_get_top_window() != s_app_data.now_playing_window) {
    s_action_feedback_timer = NULL;
    return;
  }
  
  const char *track_text = s_app_data.is_active_session ? s_app_data.track_name : DEFAULT_NO_ACTIVE_SESSION_TEXT;
  const char *artist_text = s_app_data.is_active_session ? s_app_data.artist_name : DEFAULT_START_MUSIC_TEXT;
  
  Layer *window_layer = window_get_root_layer(s_app_data.now_playing_window);
  if (!window_layer) return;
  
  GRect bounds = layer_get_bounds(window_layer);
  
  GFont track_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont artist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  int track_height = now_playing_calculate_text_height(track_text, track_font, 
                                                      bounds.size.w - ACTION_BAR_WIDTH - 20);
  int artist_height = now_playing_calculate_text_height(artist_text, artist_font, 
                                                       bounds.size.w - ACTION_BAR_WIDTH - 20);

  if (track_height < MIN_TRACK_HEIGHT) track_height = MIN_TRACK_HEIGHT;
  if (artist_height < MIN_ARTIST_HEIGHT) artist_height = MIN_ARTIST_HEIGHT;

  int max_track_height = bounds.size.h - 80;
  if (track_height > max_track_height) {
    track_height = max_track_height;
    text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeFill);
  } else {
    text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeWordWrap);
  }
  
  int max_artist_height = bounds.size.h - track_height - 20;
  if (artist_height > max_artist_height) {
    artist_height = max_artist_height;
    text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeFill);
  } else {
    text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeWordWrap);
  }
  
  if (s_track_layer) {
    layer_set_frame(text_layer_get_layer(s_track_layer), 
                   GRect(10, 5, bounds.size.w - ACTION_BAR_WIDTH - 20, track_height));
    text_layer_set_text(s_track_layer, track_text);
    text_layer_set_text_color(s_track_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  }

  if (s_artist_layer) {
    layer_set_frame(text_layer_get_layer(s_artist_layer), 
                   GRect(10, 10 + track_height, bounds.size.w - ACTION_BAR_WIDTH - 20, artist_height));
    text_layer_set_text(s_artist_layer, artist_text);
    text_layer_set_text_color(s_artist_layer, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  }
  
  if (s_action_bar_layer && !s_app_data.showing_volume_error) {
    if (!s_app_data.is_active_session) {
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, NULL);
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_SELECT, NULL);
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, NULL);
    } else {
      GBitmap *vol_up_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_vol_up_bitmap, 
                                                          RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_UP);
      GBitmap *play_pause_bitmap = app_state_get_cached_bitmap(
        s_app_data.is_playing ? &s_app_data.cached_pause_bitmap : &s_app_data.cached_play_bitmap, 
        s_app_data.is_playing ? RESOURCE_ID_IMAGE_MUSIC_ICON_PAUSE : RESOURCE_ID_IMAGE_MUSIC_ICON_PLAY);
      GBitmap *vol_down_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_vol_down_bitmap, 
                                                            RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_DOWN);
      
      if (vol_up_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, vol_up_bitmap);
      if (play_pause_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_SELECT, play_pause_bitmap);
      if (vol_down_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, vol_down_bitmap);
    }
  }
  
  s_action_feedback_timer = NULL;
}

void now_playing_update_clock(void) {
  if (!s_app_data.now_playing_window || !s_clock_layer || 
      window_stack_get_top_window() != s_app_data.now_playing_window) {
    return;
  }
  
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  
  static char clock_text[CLOCK_TEXT_SIZE];
  strftime(clock_text, sizeof(clock_text), "%H:%M", tick_time);
  text_layer_set_text(s_clock_layer, clock_text);
  
  if (s_app_data.now_playing_window && window_stack_get_top_window() == s_app_data.now_playing_window) {
    if (s_clock_timer) {
      app_timer_cancel(s_clock_timer);
      s_clock_timer = NULL;
    }
    s_clock_timer = app_timer_register(CLOCK_UPDATE_INTERVAL_MS, (AppTimerCallback)now_playing_update_clock, NULL);
  } else {
    s_clock_timer = NULL;
  }
}

// Volume Error Feedback

void now_playing_show_volume_error(ButtonId button) {
  if (!s_action_bar_layer) return;
  
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
  
  s_app_data.showing_volume_error = true;
  
  GBitmap *error_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_dismiss_bitmap, 
                                                      RESOURCE_ID_IMAGE_ICON_DISMISS);
  if (error_bitmap) {
    action_bar_layer_set_icon(s_action_bar_layer, button, error_bitmap);
  }
  
  if (s_app_data.now_playing_window && window_stack_get_top_window() == s_app_data.now_playing_window) {
    s_app_data.volume_error_timer = app_timer_register(VOLUME_ERROR_DISPLAY_MS, 
                                                       (AppTimerCallback)now_playing_clear_volume_error, NULL);
  } else {
    s_app_data.volume_error_timer = NULL;
  }
}

void now_playing_clear_volume_error(void) {
  if (!s_app_data.now_playing_window || window_stack_get_top_window() != s_app_data.now_playing_window) {
    s_app_data.showing_volume_error = false;
    s_app_data.volume_error_timer = NULL;
    return;
  }
  
  s_app_data.showing_volume_error = false;
  now_playing_update_display();
  s_app_data.volume_error_timer = NULL;
}

// Utility Functions

int now_playing_calculate_text_height(const char *text, GFont font, int width) {
  if (!text || strlen(text) == 0) {
    return TEXT_PADDING * 4;
  }

  GRect bounds = GRect(0, 0, width, MAX_TEXT_HEIGHT);
  GSize text_size = graphics_text_layout_get_content_size(text, font, bounds, 
                                                         GTextOverflowModeWordWrap, GTextAlignmentCenter);

  return text_size.h + TEXT_PADDING;
}

// Deprecated Functions (Legacy Compatibility)

void now_playing_request_volume_change(ButtonId button, int delta) {
  // Deprecated - volume changes now handled via JavaScript
}

void now_playing_apply_volume_change(void) {
  // Deprecated - volume changes now handled via JavaScript polling
}
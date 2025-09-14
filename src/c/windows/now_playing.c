#include "now_playing.h"
#include "../api/spotify_api.h"
#include "../core/app_state.h"
#include "../core/constants.h"

// Now playing UI elements
static TextLayer *s_track_layer;
static TextLayer *s_artist_layer;
static TextLayer *s_clock_layer;
static ActionBarLayer *s_action_bar_layer;
static AppTimer *s_refresh_timer;
static AppTimer *s_clock_timer;

void now_playing_init(void) {
  // Initialize now playing data
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

void now_playing_window_create(void) {
  // If window already exists, just push it to the stack
  if (s_app_data.now_playing_window) {
    window_stack_push(s_app_data.now_playing_window, true);
    return;
  }
  
  // Create new window only if it doesn't exist
  s_app_data.now_playing_window = window_create();
  window_set_window_handlers(s_app_data.now_playing_window, (WindowHandlers) {
    .load = now_playing_window_load,
    .unload = now_playing_window_unload,
  });
  
  window_stack_push(s_app_data.now_playing_window, true);
}

void now_playing_window_pop(void) {
  // Just pop the window from the stack, don't destroy it
  if (s_app_data.now_playing_window && window_stack_get_top_window() == s_app_data.now_playing_window) {
    // Clean up timers before popping to prevent timer leaks
    if (s_refresh_timer) {
      app_timer_cancel(s_refresh_timer);
      s_refresh_timer = NULL;
    }
    if (s_clock_timer) {
      app_timer_cancel(s_clock_timer);
      s_clock_timer = NULL;
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
    // First, make sure we're not on the window stack anymore
    if (window_stack_get_top_window() == s_app_data.now_playing_window) {
      window_stack_pop(true);
    }
    
    window_destroy(s_app_data.now_playing_window);
    s_app_data.now_playing_window = NULL;
    
    // Reset app state to main menu when now playing window is destroyed
    s_app_data.current_state = APP_STATE_MAIN_MENU;
  }
}

void now_playing_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create ActionBarLayer with animation
  s_action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_click_config_provider(s_action_bar_layer, now_playing_click_config_provider);
  
  // Create clock text layer at the bottom
  s_clock_layer = text_layer_create(GRect(MARGIN_MEDIUM, bounds.size.h - CLOCK_HEIGHT - MARGIN_SMALL, bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, CLOCK_HEIGHT));
  text_layer_set_text_alignment(s_clock_layer, GTextAlignmentCenter);
  text_layer_set_font(s_clock_layer, fonts_get_system_font(FONT_KEY_CLOCK));
  text_layer_set_text_color(s_clock_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  text_layer_set_background_color(s_clock_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_clock_layer));
  
  // Set press animations for better user feedback
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_UP, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_SELECT, ActionBarLayerIconPressAnimationMoveLeft);
  action_bar_layer_set_icon_press_animation(s_action_bar_layer, BUTTON_ID_DOWN, ActionBarLayerIconPressAnimationMoveLeft);
  
  action_bar_layer_add_to_window(s_action_bar_layer, window);
  
  // Create text layers for track info (ActionBarLayer takes up right side)
  GFont track_font = fonts_get_system_font(FONT_KEY_TRACK);
  GFont artist_font = fonts_get_system_font(FONT_KEY_ARTIST);
  
  // Calculate dynamic heights based on text content
  int track_height = now_playing_calculate_text_height(s_app_data.track_name, track_font, bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE);
  int artist_height = now_playing_calculate_text_height(s_app_data.artist_name, artist_font, bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE);
  
  // Ensure minimum heights
  if (track_height < MIN_TRACK_HEIGHT) track_height = MIN_TRACK_HEIGHT;
  if (artist_height < MIN_ARTIST_HEIGHT) artist_height = MIN_ARTIST_HEIGHT;
  
  s_track_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_SMALL, bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, track_height));
  text_layer_set_text_alignment(s_track_layer, GTextAlignmentCenter);
  text_layer_set_font(s_track_layer, track_font);
  text_layer_set_text_color(s_track_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  text_layer_set_background_color(s_track_layer, GColorClear);
  text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_track_layer));
  
  s_artist_layer = text_layer_create(GRect(MARGIN_MEDIUM, MARGIN_MEDIUM + track_height, bounds.size.w - ACTION_BAR_WIDTH - MARGIN_LARGE, artist_height));
  text_layer_set_text_alignment(s_artist_layer, GTextAlignmentCenter);
  text_layer_set_font(s_artist_layer, artist_font);
  text_layer_set_text_color(s_artist_layer, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  text_layer_set_background_color(s_artist_layer, GColorClear);
  text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeWordWrap);
  layer_add_child(window_layer, text_layer_get_layer(s_artist_layer));
  
  // Set background color using compile-time macros
  window_set_background_color(window, PBL_IF_COLOR_ELSE(GColorBlack, GColorWhite));
  
  // Update display
  now_playing_update_display();
  
  // Start clock timer (update every minute)
  now_playing_update_clock();
  
  // Refresh current track data
  if (s_app_data.is_authenticated) {
    spotify_api_refresh_now_playing();
    
    // Cancel any existing refresh timer before creating a new one
    if (s_refresh_timer) {
      app_timer_cancel(s_refresh_timer);
    }
    
    // Set up periodic refresh every 10 seconds
    s_refresh_timer = app_timer_register(REFRESH_INTERVAL_MS, (AppTimerCallback)spotify_api_refresh_now_playing, NULL);
  }
}

void now_playing_window_unload(Window *window) {
  // Clean up ALL timers
  if (s_refresh_timer) {
    app_timer_cancel(s_refresh_timer);
    s_refresh_timer = NULL;
  }
  if (s_clock_timer) {
    app_timer_cancel(s_clock_timer);
    s_clock_timer = NULL;
  }
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
  
  // Clean up layers
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

void now_playing_long_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  
  // Long press actions for track navigation
  if (!s_app_data.is_active_session) {
    return; // No action for long press when no active session
  }
  
  // Set long press icons before performing action
  if (s_action_bar_layer) {
    switch (button) {
      case BUTTON_ID_UP:
        // Show skip backward icon for long press
        if (s_app_data.can_skip_prev) {
          GBitmap *backward_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_backward_bitmap, RESOURCE_ID_IMAGE_MUSIC_ICON_BACKWARD);
          if (backward_bitmap) {
            action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, backward_bitmap);
          }
        }
        break;
      case BUTTON_ID_DOWN:
        // Show skip forward icon for long press
        if (s_app_data.can_skip_next) {
          GBitmap *forward_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_forward_bitmap, RESOURCE_ID_IMAGE_MUSIC_ICON_FORWARD);
          if (forward_bitmap) {
            action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, forward_bitmap);
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
        spotify_api_skip_to_previous();
      }
      break;
    case BUTTON_ID_DOWN:
      // Next track (long press)
      if (s_app_data.can_skip_next) {
        spotify_api_skip_to_next();
      }
      break;
    default:
      break;
  }
  
  // Restore original icons after a short delay to show the action was performed
  // Note: This is a one-shot timer that will clean itself up
  app_timer_register(ACTION_FEEDBACK_DELAY_MS, (AppTimerCallback)now_playing_update_display, NULL);
}

void now_playing_click_config_provider(void *context) {
  // Single clicks for volume and play/pause
  window_single_click_subscribe(BUTTON_ID_UP, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, now_playing_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, now_playing_click_handler);
  
  // Long clicks for track navigation
  window_long_click_subscribe(BUTTON_ID_UP, LONG_CLICK_DURATION_MS, now_playing_long_click_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, LONG_CLICK_DURATION_MS, now_playing_long_click_handler, NULL);
}

void now_playing_click_handler(ClickRecognizerRef recognizer, void *context) {
  ButtonId button = click_recognizer_get_button_id(recognizer);
  
  // Single click actions for volume and play/pause
  now_playing_handle_action(button);
}

void now_playing_handle_action(ButtonId button) {
  if (!s_app_data.is_active_session) {
    // Show helpful message when no active session
    strncpy(s_app_data.track_name, DEFAULT_NO_ACTIVE_SESSION_TEXT, sizeof(s_app_data.track_name) - 1);
    s_app_data.track_name[sizeof(s_app_data.track_name) - 1] = '\0';
    strncpy(s_app_data.artist_name, DEFAULT_START_MUSIC_TEXT, sizeof(s_app_data.artist_name) - 1);
    s_app_data.artist_name[sizeof(s_app_data.artist_name) - 1] = '\0';
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
      now_playing_request_volume_change(BUTTON_ID_UP, VOLUME_STEP_SIZE);
      break;
    case BUTTON_ID_SELECT:
      // Play/pause
      spotify_api_play_pause_track();
      // Don't update display immediately - let the API response handle it
      break;
    case BUTTON_ID_DOWN:
      // Volume down
      now_playing_request_volume_change(BUTTON_ID_DOWN, -VOLUME_STEP_SIZE);
      break;
    default:
      break;
  }
}

void now_playing_update_display(void) {
  if (!s_app_data.now_playing_window) {
    return;
  }
  
  // Additional safety check - ensure window is still valid
  if (window_stack_get_top_window() != s_app_data.now_playing_window) {
    // Window is not on top of stack, don't update display
    return;
  }
  
  // Determine the text content for each layer
  const char *track_text;
  const char *artist_text;
  
  if (!s_app_data.is_active_session) {
    track_text = DEFAULT_NO_ACTIVE_SESSION_TEXT;
    artist_text = DEFAULT_START_MUSIC_TEXT;
  } else {
    track_text = s_app_data.track_name;
    artist_text = s_app_data.artist_name;
  }
  
  // Get window bounds for width calculation
  Layer *window_layer = window_get_root_layer(s_app_data.now_playing_window);
  if (!window_layer) {
    return; // Window layer is invalid
  }
  
  GRect bounds = layer_get_bounds(window_layer);
  
  // Calculate dynamic heights for the new text content
  GFont track_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont artist_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

  int track_height = now_playing_calculate_text_height(track_text, track_font, bounds.size.w - ACTION_BAR_WIDTH - 20);
  int artist_height = now_playing_calculate_text_height(artist_text, artist_font, bounds.size.w - ACTION_BAR_WIDTH - 20);

  // Ensure minimum heights
  if (track_height < MIN_TRACK_HEIGHT) track_height = MIN_TRACK_HEIGHT;
  if (artist_height < MIN_ARTIST_HEIGHT) artist_height = MIN_ARTIST_HEIGHT;

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
    text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeFill);
  } else {
    // Keep word wrap mode for track text
    text_layer_set_overflow_mode(s_track_layer, GTextOverflowModeWordWrap);
  }
  
  if (artist_height > max_artist_height) {
    artist_height = max_artist_height;
    // Switch to ellipsis mode for artist text
    text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeFill);
  } else {
    // Keep word wrap mode for artist text
    text_layer_set_overflow_mode(s_artist_layer, GTextOverflowModeWordWrap);
  }
  
  // Resize and reposition text layers
  if (s_track_layer) {
    layer_set_frame(text_layer_get_layer(s_track_layer), GRect(10, 5, bounds.size.w - ACTION_BAR_WIDTH - 20, track_height));
    text_layer_set_text(s_track_layer, track_text);
    text_layer_set_text_color(s_track_layer, PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack));
  }

  if (s_artist_layer) {
    layer_set_frame(text_layer_get_layer(s_artist_layer), GRect(10, 10 + track_height, bounds.size.w - ACTION_BAR_WIDTH - 20, artist_height));
    text_layer_set_text(s_artist_layer, artist_text);
    text_layer_set_text_color(s_artist_layer, PBL_IF_COLOR_ELSE(GColorLightGray, GColorDarkGray));
  }
  
  // Update ActionBarLayer with new simplified button behavior
  // Skip updating action bar icons if we're showing a volume error
  if (s_action_bar_layer && !s_app_data.showing_volume_error) {
    if (!s_app_data.is_active_session) {
      // No active session - clear all icons
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, NULL);
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_SELECT, NULL);
      action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, NULL);
    } else {
      // New simplified behavior: Volume Up, Play/Pause, Volume Down (no animation on set)
      GBitmap *vol_up_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_vol_up_bitmap, RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_UP);
      GBitmap *play_pause_bitmap = app_state_get_cached_bitmap(
        s_app_data.is_playing ? &s_app_data.cached_pause_bitmap : &s_app_data.cached_play_bitmap, 
        s_app_data.is_playing ? RESOURCE_ID_IMAGE_MUSIC_ICON_PAUSE : RESOURCE_ID_IMAGE_MUSIC_ICON_PLAY);
      GBitmap *vol_down_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_vol_down_bitmap, RESOURCE_ID_IMAGE_MUSIC_ICON_VOLUME_DOWN);
      
      if (vol_up_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP, vol_up_bitmap);
      if (play_pause_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_SELECT, play_pause_bitmap);
      if (vol_down_bitmap) action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, vol_down_bitmap);
    }
  }
}

void now_playing_update_clock(void) {
  // Safety check - only update clock if window and layer are still valid
  if (!s_app_data.now_playing_window || !s_clock_layer || window_stack_get_top_window() != s_app_data.now_playing_window) {
    return;
  }
  
  time_t now = time(NULL);
  struct tm *tick_time = localtime(&now);
  
  static char clock_text[CLOCK_TEXT_SIZE];
  strftime(clock_text, sizeof(clock_text), "%H:%M", tick_time);
  
  text_layer_set_text(s_clock_layer, clock_text);
  
  // Cancel the previous timer before registering a new one
  if (s_clock_timer) {
    app_timer_cancel(s_clock_timer);
  }
  
  // Restart timer for next update (every minute)
  s_clock_timer = app_timer_register(CLOCK_UPDATE_INTERVAL_MS, (AppTimerCallback)now_playing_update_clock, NULL);
}

void now_playing_request_volume_change(ButtonId button, int delta) {
  // If there's already a volume change pending, accumulate the delta locally
  if (s_app_data.volume_change_pending) {
    s_app_data.pending_volume_delta += delta;
    // APP_LOG(APP_LOG_LEVEL_INFO, "Accumulating volume change: delta=%d, total=%d", delta, s_app_data.pending_volume_delta);
    return;
  }
  
  // Start a new volume change request
  s_app_data.volume_change_pending = true;
  s_app_data.pending_volume_button = button;
  s_app_data.pending_volume_delta = delta;
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Requesting volume change: button=%d, delta=%d", button, delta);
  
  // Only fetch current volume if we haven't fetched it recently (within last 2 seconds)
  time_t current_time = time(NULL);
  if (current_time - s_app_data.last_volume_fetch_time > VOLUME_FETCH_INTERVAL_MS / 1000) {
    s_app_data.last_volume_fetch_time = current_time;
    spotify_api_make_call("/me/player", "GET", NULL);
  } else {
    // Use current local volume and apply change immediately
    // APP_LOG(APP_LOG_LEVEL_INFO, "Using local volume (recent fetch), applying change immediately");
    now_playing_apply_volume_change();
  }
}

void now_playing_apply_volume_change(void) {
  if (!s_app_data.volume_change_pending) {
    return;
  }
  
  // Calculate new volume based on current API volume + accumulated delta
  int new_volume = s_app_data.volume_percent + s_app_data.pending_volume_delta;
  
  // Clamp to valid range
  if (new_volume < VOLUME_MIN) new_volume = VOLUME_MIN;
  if (new_volume > VOLUME_MAX) new_volume = VOLUME_MAX;
  
  // APP_LOG(APP_LOG_LEVEL_INFO, "Applying volume change: current=%d, delta=%d, new=%d",
  //         s_app_data.volume_percent, s_app_data.pending_volume_delta, new_volume);
  
  // Update local volume immediately for UI feedback
  s_app_data.volume_percent = new_volume;
  
  // Update display immediately to show new volume
  now_playing_update_display();
  
  // Send volume change to API
  spotify_api_set_volume(new_volume, s_app_data.pending_volume_button);
  
  // Clear pending state
  s_app_data.volume_change_pending = false;
  s_app_data.pending_volume_delta = 0;
  
  // Refresh now playing data after volume change
  // Note: This is a one-shot timer that will clean itself up
  app_timer_register(API_RETRY_DELAY_MS, (AppTimerCallback)spotify_api_refresh_now_playing, NULL);
}

void now_playing_show_volume_error(ButtonId button) {
  if (!s_action_bar_layer) {
    return;
  }
  
  APP_LOG(APP_LOG_LEVEL_INFO, "Showing volume error for button %d", button);
  
  // Cancel any existing volume error timer first
  if (s_app_data.volume_error_timer) {
    app_timer_cancel(s_app_data.volume_error_timer);
    s_app_data.volume_error_timer = NULL;
  }
  
  // Set flag to prevent display updates from overriding error icon
  s_app_data.showing_volume_error = true;
  
  // Show dismiss icon as error indicator
  GBitmap *error_bitmap = app_state_get_cached_bitmap(&s_app_data.cached_dismiss_bitmap, RESOURCE_ID_IMAGE_ICON_DISMISS);
  if (error_bitmap) {
    action_bar_layer_set_icon(s_action_bar_layer, button, error_bitmap);
  }
  
  // Clear the error after 1 second
  s_app_data.volume_error_timer = app_timer_register(VOLUME_ERROR_DISPLAY_MS, (AppTimerCallback)now_playing_clear_volume_error, NULL);
}

void now_playing_clear_volume_error(void) {
  // Safety check - only clear error if window is still valid
  if (!s_app_data.now_playing_window || window_stack_get_top_window() != s_app_data.now_playing_window) {
    s_app_data.showing_volume_error = false;
    s_app_data.volume_error_timer = NULL;
    return;
  }
  
  // Clear the error flag
  s_app_data.showing_volume_error = false;
  
  // Restore normal display
  now_playing_update_display();
  s_app_data.volume_error_timer = NULL;
}

int now_playing_calculate_text_height(const char *text, GFont font, int width) {
  if (!text || strlen(text) == 0) {
    return TEXT_PADDING * 4; // Default height for empty text
  }

  // Use graphics_text_layout_get_content_size to calculate the height needed
  GRect bounds = GRect(0, 0, width, MAX_TEXT_HEIGHT); // Large height to measure
  GSize text_size = graphics_text_layout_get_content_size(text, font, bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter);

  return text_size.h + TEXT_PADDING; // Add small padding
}

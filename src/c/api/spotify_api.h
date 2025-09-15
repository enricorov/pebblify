#ifndef SPOTIFY_API_H
#define SPOTIFY_API_H

#include <pebble.h>
#include "../core/app_state.h"

// Spotify API functions
void spotify_api_init(void);
void spotify_api_deinit(void);
void spotify_api_make_call(const char *path, const char *method, const char *data);
void spotify_api_refresh_now_playing(void);
void spotify_api_play_pause_track(void);
void spotify_api_skip_to_next(void);
void spotify_api_skip_to_previous(void);
void spotify_api_volume_up(void);
void spotify_api_volume_down(void);
void spotify_api_set_volume(int volume_percent, ButtonId button);

// Message handling functions
void spotify_api_handle_response(DictionaryIterator *iter);
void spotify_api_handle_error(DictionaryIterator *iter);
void spotify_api_app_message_handler(DictionaryIterator *iter, void *context);
void spotify_api_app_message_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context);
void spotify_api_app_message_outbox_sent(DictionaryIterator *iter, void *context);

#endif // SPOTIFY_API_H

// Pebblify Authentication Module
var SpotifyAuth = require('./auth.js');
var NowPlayingManager = require('./now_playing.js');

// Initialize the app
console.log('Pebblify JavaScript app started');

// Set up integration between auth and now playing
var spotifyAuth = new SpotifyAuth();

// When authentication succeeds, start now playing polling
Pebble.addEventListener('appmessage', function(e) {
  var message = e.payload;
  if (message[require('message_keys').AUTH_SUCCESS] !== undefined) {
    // Authentication succeeded, start now playing polling
    NowPlayingManager.startPolling();
  }
});

// Export modules for potential external use
module.exports = {
  SpotifyAuth: SpotifyAuth,
  NowPlayingManager: NowPlayingManager
};

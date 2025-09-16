// Pebblify Authentication Module
console.log('Pebblify JavaScript app started - basic test');

// Provide configuration URL to pypkjs for emu-app-config
if (typeof Pebble !== 'undefined' && Pebble.config) {
  console.log('Setting up Pebble.config');
  Pebble.config({
    url: 'file:///home/rebble/dev/pebblify/src/pkjs/clay-config.html'
  });
  console.log('Pebble.config set successfully');
} else {
  console.log('Pebble.config not available');
}

// Basic module loading test
try {
  var SpotifyAuth = require('./auth.js');
  var NowPlayingManager = require('./now_playing.js');
  console.log('Modules loaded successfully');
} catch (e) {
  console.log('Module loading error:', e.message);
}

// Initialize the app
var spotifyAuth;
try {
  spotifyAuth = new SpotifyAuth();
  console.log('SpotifyAuth initialized');
} catch (e) {
  console.log('SpotifyAuth error:', e.message);
}

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

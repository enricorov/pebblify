// Spotify Authentication for Pebblify C App
// Use native Pebble API instead of PebbleJS to avoid module loading issues
var axios = require('axios');
var Settings = require('pebblejs/settings');
var sha256 = require('./sha256');
var constants = require('./constants');
var messageKeys = require('message_keys');

// Check if axios is available
if (typeof axios === 'undefined') {
  console.log('ERROR: axios module not loaded properly');
} else {
  console.log('axios module loaded successfully');
}

// Spotify API constants
var CLIENT_ID = constants.SPOTIFY_CONFIG.CLIENT_ID;
var PEBBLE_REDIRECT_URI = constants.SPOTIFY_CONFIG.PEBBLE_REDIRECT_URI;
var SCOPES = constants.SPOTIFY_CONFIG.SCOPES;
var ACCOUNTS_BASE_URL = constants.SPOTIFY_CONFIG.ACCOUNTS_BASE_URL;
var API_BASE_URL = constants.SPOTIFY_CONFIG.API_BASE_URL;

// Message keys for communication with C app
// These are auto-generated from package.json and available as global variables

function SpotifyAuth() {
  // console.log('SpotifyAuth constructor called');
  this.accessToken = null;
  this.refreshToken = null;
  this.tokenExpiresAt = null;
  
  // Clear any raw URL parameters from localStorage before doing anything else
  for (var i = localStorage.length - 1; i >= 0; i--) {
    var key = localStorage.key(i);
    var value = localStorage.getItem(key);
    if (value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
    // Also clear Settings library keys that might contain raw URL parameters
    if (key && (key.startsWith('options:') || key.startsWith('data:')) && value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
  }
  
  this.setupAppMessageHandlers();
  this.initSettingsPage();
  this.loadStoredTokens(); // Load tokens from localStorage on startup
  // console.log('SpotifyAuth constructor completed');
}

SpotifyAuth.prototype.setupAppMessageHandlers = function() {
  var self = this;
  // Handle authentication requests from C app
  Pebble.addEventListener('appmessage', function(e) {
    var message = e.payload;
    // console.log('JS: Received message from C app:', message);
    // console.log('JS: Message keys available:', Object.keys(message));
    // console.log('JS: messageKeys.AUTH_REQUEST =', messageKeys.AUTH_REQUEST);
    // console.log('JS: messageKeys.API_CALL =', messageKeys.API_CALL);
    // console.log('JS: messageKeys.TOKEN_REFRESH =', messageKeys.TOKEN_REFRESH);
    
    // Check for AUTH_REQUEST (try both numeric and string keys)
    if (message[messageKeys.AUTH_REQUEST] !== undefined || message['AUTH_REQUEST'] !== undefined) {
      // console.log('JS: Received AUTH_REQUEST');
      self.handleAuthRequest();
    }
    // Check for TOKEN_REFRESH (try both numeric and string keys)
    else if (message[messageKeys.TOKEN_REFRESH] !== undefined || message['TOKEN_REFRESH'] !== undefined) {
      // console.log('JS: Received TOKEN_REFRESH');
      self.refreshAccessToken();
    }
    // Check for API_CALL (try both numeric and string keys)
    else if (message[messageKeys.API_CALL] !== undefined || message['API_CALL'] !== undefined) {
      // console.log('JS: Received API_CALL');
      self.handleApiCall(message);
    }
    else {
      // console.log('JS: Received unknown message type');
    }
  });
};

SpotifyAuth.prototype.initSettingsPage = function() {
  var self = this;
  
  // Generate the authorization URL with PKCE parameters ONCE
  this.authUrl = this.getAuthorizationUrl();
  
  // Check if we're in emulator mode (Pebble.config available)
  if (typeof Pebble !== 'undefined' && Pebble.config) {
    // Use configuration URL for emulator
    Pebble.config({
      url: 'file:///home/rebble/dev/pebblify/src/pkjs/clay-config.html'
    });
  } else {
    // Use native Pebble Settings API for real devices - configure ONCE
    try {
      Settings.config({
        url: this.authUrl,
        autosave: false,
        hash: true,
      }, function(e) {
        // Settings config opened successfully
      }, function(e) {
        if (e.failed) {
          console.log('Settings parsing failed:', e.response);
        }
        if (e.options.hasOwnProperty('/?code')) {
          // user accepted authorization, code received
          var pkceCode = e.options['/?code'];
          self.getToken(pkceCode);
        } else if (e.options.hasOwnProperty('/?error')) {
          // user closed authorization url
        }
      });
    } catch (error) {
      console.log('Error calling Settings API:', error);
    }
  }
};

SpotifyAuth.prototype.getAuthorizationUrl = function() {
  // Create and store a random "state" value
  var state = this.generateRandomString(constants.PKCE_CONFIG.STATE_LENGTH);
  localStorage.setItem('pkceState', state);

  // Create and store a new PKCE code_verifier (the plaintext random secret)
  var codeVerifier = this.generateRandomString(constants.PKCE_CONFIG.CODE_VERIFIER_LENGTH);
  localStorage.setItem('pkceCodeVerifier', codeVerifier);

  // Hash and base64-urlencode the secret to use as the challenge
  var codeChallenge = this.pkceChallengeFromVerifier(codeVerifier);

  return ACCOUNTS_BASE_URL + '/authorize?client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&scope=' + encodeURIComponent(SCOPES.join(' ')) + '&code_challenge=' + codeChallenge + '&code_challenge_method=S256&state=123&response_type=code';
};

SpotifyAuth.prototype.getToken = function(pkceCode) {
  var self = this;
  var codeVerifier = localStorage.getItem('pkceCodeVerifier');
  
  if (!codeVerifier) {
    console.log('ERROR: No code verifier found in localStorage');
    self.sendAuthError('No code verifier found');
    return;
  }
  
  if (typeof axios === 'undefined') {
    console.log('ERROR: axios is not available');
    self.sendAuthError('HTTP client not available');
    return;
  }
  
  var body = 'client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&code_verifier=' + codeVerifier + '&code=' + pkceCode + '&grant_type=authorization_code';
  axios.post(ACCOUNTS_BASE_URL + '/api/token', body, {
    headers: {
      'content-type': 'application/x-www-form-urlencoded',
    },
  }).then(function(response) {
    var userTokens = response.data;
    userTokens.expiration_date = Date.now() + userTokens.expires_in * 1000;

    localStorage.setItem('userTokens', JSON.stringify(userTokens));
    self.accessToken = userTokens.access_token;
    self.refreshToken = userTokens.refresh_token;
    self.tokenExpiresAt = userTokens.expiration_date;
    
    // Log the token before sending to C app
    console.log('Access token:', self.accessToken);
    console.log('Refresh token:', self.refreshToken);
    
    // Clear the PKCE code verifier since we've successfully used it
    localStorage.removeItem('pkceCodeVerifier');
    localStorage.removeItem('pkceState');
    
    self.sendAuthSuccess();
  }).catch(function(data) {
    console.log('Token request failed:', data);
    if (data.response && data.response.data && data.response.data.error_description) {
      console.log('Error:', data.response.data.error_description);
    }
    var errorMessage = data.response && data.response.data && data.response.data.error_description 
      ? data.response.data.error_description 
      : (data.error || 'Token request failed');
    self.sendAuthError(errorMessage);
  });
};

SpotifyAuth.prototype.handleAuthRequest = function() {
  // First, try to load tokens from localStorage
  this.loadStoredTokens();
  
  // Check if we already have valid tokens
  if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
    this.sendAuthSuccess();
    return;
  }
  
  // Check if we're in emulator mode (Pebble.config available)
  if (typeof Pebble !== 'undefined' && Pebble.config) {
    // Emulator mode - settings already configured
  } else {
    // For real devices, just open the already-configured settings page
    try {
      if (typeof Settings.settingsUrl === 'function') {
        Settings.settingsUrl();
      } else if (typeof Settings.onOpenConfig === 'function') {
        Settings.onOpenConfig();
      }
    } catch (error) {
      console.log('Error opening settings:', error);
    }
  }
};

SpotifyAuth.prototype.refreshAccessToken = function() {
  var self = this;
  if (!this.refreshToken) {
    this.sendAuthError('No refresh token available');
    return;
  }

  axios.post(ACCOUNTS_BASE_URL + '/api/token',
    'client_id=' + CLIENT_ID + '&' +
    'refresh_token=' + this.refreshToken + '&' +
    'grant_type=refresh_token',
    {
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      }
    }
  ).then(function(response) {
    var tokens = response.data;
    self.accessToken = tokens.access_token;
    self.tokenExpiresAt = Date.now() + (tokens.expires_in * 1000);

    // Update stored tokens
    localStorage.setItem('spotify_access_token', self.accessToken);
    localStorage.setItem('spotify_token_expires_at', self.tokenExpiresAt);

    self.sendAuthSuccess();
  }).catch(function(error) {
    console.error('Token refresh failed:', error);
    self.sendAuthError('Failed to refresh token');
  });
};

SpotifyAuth.prototype.handleApiCall = function(message) {
  // console.log('JS: Handling API call:', message);
  var self = this;
  if (!this.accessToken) {
    // console.log('JS: No access token available');
    this.sendApiError('No access token available');
    return;
  }

  // Extract API call details from message (try both numeric and string keys)
  var apiPath = message[messageKeys.API_PATH] || message['API_PATH'] || message.api_path;
  var httpMethod = message[messageKeys.HTTP_METHOD] || message['HTTP_METHOD'] || message.http_method || 'GET';
  var data = message[messageKeys.API_DATA] || message['API_DATA'] || message.data || {};
  
  // console.log('Making API call:', httpMethod, API_BASE_URL + apiPath);
  
  axios({
    url: API_BASE_URL + apiPath,
    method: httpMethod,
    headers: {
      'Authorization': 'Bearer ' + this.accessToken,
      'Content-Type': 'application/json'
    },
    data: data
  }).then(function(response) {
    // console.log('API call successful:', response.data);
    self.sendParsedNowPlayingData(response.data);
  }).catch(function(error) {
    console.error('API call failed:', error);
    
    if (error.response && error.response.status === constants.HTTP_STATUS.UNAUTHORIZED) {
      // Token expired, try to refresh
      self.refreshAccessToken();
      // Don't retry automatically - let the user trigger the action again
    } else {
      self.sendApiError(error.message);
    }
  });
};

// Volume control is now handled by NowPlayingManager


// Utility functions
SpotifyAuth.prototype.generateRandomString = function(length) {
  var charset = constants.PKCE_CONFIG.CHARSET;
  var result = '';
  for (var i = 0; i < length; i++) {
    result += charset.charAt(Math.floor(Math.random() * charset.length));
  }
  return result;
};

// Simple base64 encoding for pypkjs compatibility
function base64Encode(str) {
  var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  var result = '';
  var i = 0;
  
  while (i < str.length) {
    var a = str.charCodeAt(i++);
    var b = i < str.length ? str.charCodeAt(i++) : 0;
    var c = i < str.length ? str.charCodeAt(i++) : 0;
    
    var bitmap = (a << 16) | (b << 8) | c;
    
    result += chars.charAt((bitmap >> 18) & 63);
    result += chars.charAt((bitmap >> 12) & 63);
    result += i - 2 < str.length ? chars.charAt((bitmap >> 6) & 63) : '=';
    result += i - 1 < str.length ? chars.charAt(bitmap & 63) : '=';
  }
  
  return result;
}

SpotifyAuth.prototype.pkceChallengeFromVerifier = function(verifier) {
  // For PKCE with S256 method, hash the verifier with SHA256 and base64url encode
  // Convert string to bytes (simple implementation for pypkjs compatibility)
  var data = new Uint8Array(verifier.length);
  for (var i = 0; i < verifier.length; i++) {
    data[i] = verifier.charCodeAt(i);
  }
  var hashed = sha256(data);
  
  // Use btoa if available (real devices), otherwise use our custom base64 encoder
  var hashBytes = new Uint8Array(hashed);
  var base64;
  
  if (typeof btoa !== 'undefined') {
    // Real device - use native btoa
    base64 = btoa(String.fromCharCode.apply(null, hashBytes));
  } else {
    // pypkjs - use custom encoder
    var hashString = '';
    for (var j = 0; j < hashBytes.length; j++) {
      hashString += String.fromCharCode(hashBytes[j]);
    }
    base64 = base64Encode(hashString);
  }
  
  return base64
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
};

// Message sending functions
SpotifyAuth.prototype.sendAuthSuccess = function() {
  
  var message = {};
  message[messageKeys.AUTH_SUCCESS] = 1;
  message[messageKeys.ACCESS_TOKEN] = this.accessToken;
  message[messageKeys.REFRESH_TOKEN] = this.refreshToken;
  message[messageKeys.EXPIRES_AT] = this.tokenExpiresAt;
  Pebble.sendAppMessage(message);
  
  // Start now playing polling if available
  if (typeof require !== 'undefined') {
    try {
      var NowPlayingManager = require('./now_playing.js');
      NowPlayingManager.setAccessToken(this.accessToken);
      NowPlayingManager.startPolling();
    } catch (e) {
      console.log('Now playing manager not available:', e);
    }
  }
  
  // console.log('Auth success message sent to C app');
};

SpotifyAuth.prototype.sendAuthError = function(error) {
  var message = {};
  message[messageKeys.AUTH_ERROR] = 1;
  message[messageKeys.ERROR_MESSAGE] = error;
  Pebble.sendAppMessage(message);
};

SpotifyAuth.prototype.sendApiResponse = function(data) {
  var message = {};
  message[messageKeys.API_RESPONSE] = 1;
  message[messageKeys.RESPONSE_DATA] = JSON.stringify(data);
  Pebble.sendAppMessage(message);
};

SpotifyAuth.prototype.sendParsedNowPlayingData = function(data) {
  // console.log('JS: Sending API_RESPONSE message to C app');
  // console.log('Parsing now playing data:', data);
  
  // Parse the Spotify API response
  var trackName = constants.DEFAULT_TEXTS.NO_SESSION;
  var artistName = '';
  var isPlaying = false;
  var volumePercent = constants.VOLUME_CONFIG.DEFAULT;
  var canSkipPrev = true;
  var canSkipNext = true;
  
  if (data && data.item) {
    trackName = data.item.name || '';
    if (data.item.artists && data.item.artists.length > 0) {
      artistName = data.item.artists[0].name || '';
    }
  }
  
  if (data) {
    isPlaying = data.is_playing || false;
    // console.log('Device data:', data.device);
    if (data.device && data.device.volume_percent !== undefined) {
      volumePercent = data.device.volume_percent;
    }
    if (data.actions && data.actions.disallows) {
      canSkipPrev = !data.actions.disallows.skipping_prev;
      canSkipNext = !data.actions.disallows.skipping_next;
    }
  }
  
  // console.log('Parsed data:', {
  //   trackName: trackName,
  //   artistName: artistName,
  //   isPlaying: isPlaying,
  //   volumePercent: volumePercent,
  //   canSkipPrev: canSkipPrev,
  //   canSkipNext: canSkipNext
  // });
  
  var messageToSend = {};
  messageToSend[messageKeys.API_RESPONSE] = 1;
  messageToSend[messageKeys.TRACK_NAME] = trackName;
  messageToSend[messageKeys.ARTIST_NAME] = artistName;
  messageToSend[messageKeys.IS_PLAYING] = isPlaying ? 1 : 0;
  messageToSend[messageKeys.VOLUME_PERCENT] = volumePercent;
  messageToSend[messageKeys.CAN_SKIP_PREV] = canSkipPrev ? 1 : 0;
  messageToSend[messageKeys.CAN_SKIP_NEXT] = canSkipNext ? 1 : 0;
  
  // console.log('Sending message to C app:', messageToSend);
  // console.log('Volume value being sent:', volumePercent, 'Type:', typeof volumePercent);
  
  // Send parsed data to C app
  Pebble.sendAppMessage(messageToSend);
};

SpotifyAuth.prototype.sendApiError = function(error) {
  var message = {};
  message[messageKeys.API_ERROR] = 1;
  message[messageKeys.ERROR_MESSAGE] = error;
  Pebble.sendAppMessage(message);
};

// Load stored tokens on startup
SpotifyAuth.prototype.loadStoredTokens = function() {
  // First, clear any raw URL parameters from any localStorage keys
  for (var i = localStorage.length - 1; i >= 0; i--) {
    var key = localStorage.key(i);
    var value = localStorage.getItem(key);
    if (value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
    // Also clear Settings library keys that might contain raw URL parameters
    if (key && (key.startsWith('options:') || key.startsWith('data:')) && value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
  }
  
  var userTokensStr = localStorage.getItem('userTokens');
  if (userTokensStr) {
    // Check if this is raw URL parameters instead of JSON
    if (userTokensStr.startsWith('/?code=')) {
      localStorage.removeItem('userTokens');
      return;
    }
    
    try {
      var userTokens = JSON.parse(userTokensStr);
      var newAccessToken = userTokens.access_token;
      var newRefreshToken = userTokens.refresh_token;
      var newTokenExpiresAt = userTokens.expiration_date;
    } catch (e) {
      console.log('Invalid JSON in localStorage:', e.message, userTokensStr);
      // Clear invalid data
      localStorage.removeItem('userTokens');
      return;
    }
    
    // Check if tokens have changed
    var tokensChanged = false;
    if (this.accessToken !== newAccessToken) {
      this.accessToken = newAccessToken;
      tokensChanged = true;
    }
    
    if (this.refreshToken !== newRefreshToken) {
      this.refreshToken = newRefreshToken;
      tokensChanged = true;
    }
    
    if (this.tokenExpiresAt !== newTokenExpiresAt) {
      this.tokenExpiresAt = newTokenExpiresAt;
      tokensChanged = true;
    }
    
    // If tokens are valid, send them to C app (only if they changed or this is first load)
    if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
      if (tokensChanged || !this.accessToken) {
        this.sendAuthSuccess();
      }
    } 
    // If tokens exist but are expired, try to refresh automatically
    else if (this.accessToken && this.refreshToken && this.tokenExpiresAt && Date.now() >= this.tokenExpiresAt) {
      console.log('Access token expired, attempting refresh on startup');
      this.refreshAccessToken();
    }
  }
};


// Initialize authentication when app starts
Pebble.addEventListener('ready', function() {
  // Clear only stale authorization codes, preserve PKCE parameters generated in constructor
  for (var i = localStorage.length - 1; i >= 0; i--) {
    var key = localStorage.key(i);
    var value = localStorage.getItem(key);
    if (value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
    // Also clear Settings library keys that might contain raw URL parameters
    if (key && (key.startsWith('options:') || key.startsWith('data:')) && value && value.startsWith('/?code=')) {
      localStorage.removeItem(key);
    }
  }
});

// Export for module use
module.exports = SpotifyAuth;
// Spotify Authentication for Pebblify C App
// Use native Pebble API instead of PebbleJS to avoid module loading issues
var axios = require('axios');
var Settings = require('pebblejs/settings');
var sha256 = require('./sha256');

// Spotify API constants
var CLIENT_ID = '152d31f9089d4be0b6605671dae99c3f';
var PEBBLE_REDIRECT_URI = 'pebblejs://close';
var SCOPES = [
  'user-read-private',
  'user-read-email',
  'user-read-playback-state',
  'user-modify-playback-state',
  'user-read-currently-playing',
  'playlist-read-private',
  'playlist-read-collaborative',
];
var ACCOUNTS_BASE_URL = 'https://accounts.spotify.com';
var API_BASE_URL = 'https://api.spotify.com/v1';

// Message keys for communication with C app
var MESSAGE_KEYS = {
  AUTH_REQUEST: 0,
  AUTH_SUCCESS: 1,
  AUTH_ERROR: 2,
  TOKEN_REFRESH: 3,
  API_CALL: 4,
  API_RESPONSE: 5,
  API_ERROR: 6
};

function SpotifyAuth() {
  console.log('SpotifyAuth constructor called');
  this.accessToken = null;
  this.refreshToken = null;
  this.tokenExpiresAt = null;
  this.setupAppMessageHandlers();
  this.initSettingsPage();
  console.log('SpotifyAuth constructor completed');
}

SpotifyAuth.prototype.setupAppMessageHandlers = function() {
  var self = this;
  // Handle authentication requests from C app
  Pebble.addEventListener('appmessage', function(e) {
    var message = e.payload;
    console.log('Received message from C app:', message);
    
    // Check for AUTH_REQUEST (key 0)
    if (message[0] !== undefined) {
      console.log('Received AUTH_REQUEST');
      self.handleAuthRequest();
    }
    // Check for TOKEN_REFRESH (key 3)
    else if (message[3] !== undefined) {
      console.log('Received TOKEN_REFRESH');
      self.refreshAccessToken();
    }
    // Check for API_CALL (key 4)
    else if (message[4] !== undefined) {
      console.log('Received API_CALL');
      self.handleApiCall(message);
    }
    else {
      console.log('Received unknown message type');
    }
  });
};

SpotifyAuth.prototype.initSettingsPage = function() {
  console.log('initSettingsPage called');
  var self = this;
  this.authUrl = this.getAuthorizationUrl();
  console.log('Authorization URL:', this.authUrl);
  
  // Use native Pebble Settings API
  Settings.config({
    url: this.authUrl,
    autosave: false,
    hash: true,
  }, function(e) {
    console.log('opening configurable');
  }, function(e) {
    console.log('Settings callback received:', e);
    if (e.options.hasOwnProperty('/?code')) {
      // user accepted authorization, code received
      var pkceCode = e.options['/?code'];
      console.log('Authorization code received:', pkceCode);
      self.getToken(pkceCode);
    } else if (e.options.hasOwnProperty('/?error')) {
      // user closed authorization url
      console.log('User closed Spotify authorize url');
    }
    if (e.failed) {
      console.log('PARSING FAILED - Response:');
      console.log(e.response);
    }
  });
};

SpotifyAuth.prototype.getAuthorizationUrl = function() {
  // Create and store a random "state" value
  var state = this.generateRandomString(16);
  localStorage.setItem('pkceState', state);

  // Create and store a new PKCE code_verifier (the plaintext random secret)
  var codeVerifier = this.generateRandomString(128);
  localStorage.setItem('pkceCodeVerifier', codeVerifier);

  // Hash and base64-urlencode the secret to use as the challenge
  var codeChallenge = this.pkceChallengeFromVerifier(codeVerifier);

  return ACCOUNTS_BASE_URL + '/authorize?client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&scope=' + encodeURIComponent(SCOPES.join(' ')) + '&code_challenge=' + codeChallenge + '&code_challenge_method=S256&state=123&response_type=code';
};

SpotifyAuth.prototype.getToken = function(pkceCode) {
  console.log('getToken called with code:', pkceCode);
  var self = this;
  var codeVerifier = localStorage.getItem('pkceCodeVerifier');
  console.log('Using code verifier:', codeVerifier);
  var body = 'client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&code_verifier=' + codeVerifier + '&code=' + pkceCode + '&grant_type=authorization_code';

  console.log('Making token request to:', ACCOUNTS_BASE_URL + '/api/token');
  axios.post(ACCOUNTS_BASE_URL + '/api/token', body, {
    headers: {
      'content-type': 'application/x-www-form-urlencoded',
    },
  }).then(function(response) {
    console.log('Token response received:', response.data);
    var userTokens = response.data;
    userTokens.expiration_date = Date.now() + userTokens.expires_in * 1000;

    localStorage.setItem('userTokens', JSON.stringify(userTokens));
    self.accessToken = userTokens.access_token;
    self.refreshToken = userTokens.refresh_token;
    self.tokenExpiresAt = userTokens.expiration_date;
    
    console.log('Tokens stored, calling sendAuthSuccess');
    self.sendAuthSuccess();
  }).catch(function(data) {
    console.log('Token request failed:', data);
    if (data.error == 'invalid_grant') {
      // Authorization code expired
      console.log('User must relaunch Pebblify settings app');
    }
  });
};

SpotifyAuth.prototype.handleAuthRequest = function() {
  console.log('handleAuthRequest called');
  
  // Check if we already have valid tokens
  if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
    console.log('Already authenticated, sending success');
    this.sendAuthSuccess();
    return;
  }

  console.log('Opening settings for authentication');
  // Use PebbleJS Settings to open configuration page
  if (typeof Settings.settingsUrl === 'function') {
    console.log('Using Settings.settingsUrl()');
    Settings.settingsUrl();
  } else if (typeof Settings.onOpenConfig === 'function') {
    console.log('Using Settings.onOpenConfig()');
    Settings.onOpenConfig();
  } else {
    console.log('No suitable Settings method found');
    console.log('Authorization URL:', this.authUrl);
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
  console.log('handleApiCall called with message:', message);
  var self = this;
  if (!this.accessToken) {
    console.log('No access token available');
    this.sendApiError('No access token available');
    return;
  }

  // Extract API call details from message
  var apiPath = message[10] || message.api_path;
  var httpMethod = message[11] || message.http_method || 'GET';
  var data = message[12] || message.data || {};
  
  console.log('Making API call:', httpMethod, API_BASE_URL + apiPath);
  
  axios({
    url: API_BASE_URL + apiPath,
    method: httpMethod,
    headers: {
      'Authorization': 'Bearer ' + this.accessToken,
      'Content-Type': 'application/json'
    },
    data: data
  }).then(function(response) {
    console.log('API call successful:', response.data);
    self.sendParsedNowPlayingData(response.data);
  }).catch(function(error) {
    console.error('API call failed:', error);
    
    if (error.response && error.response.status === 401) {
      // Token expired, try to refresh
      self.refreshAccessToken();
      // Retry the API call
      setTimeout(function() {
        self.handleApiCall(message);
      }, 1000);
    } else {
      self.sendApiError(error.message);
    }
  });
};

// Utility functions
SpotifyAuth.prototype.generateRandomString = function(length) {
  var charset = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~';
  var result = '';
  for (var i = 0; i < length; i++) {
    result += charset.charAt(Math.floor(Math.random() * charset.length));
  }
  return result;
};

SpotifyAuth.prototype.pkceChallengeFromVerifier = function(verifier) {
  // For PKCE with S256 method, hash the verifier with SHA256 and base64url encode
  var encoder = new TextEncoder();
  var data = encoder.encode(verifier);
  var hashed = sha256(data);
  
  // Convert ArrayBuffer to base64url
  return btoa(String.fromCharCode.apply(null, new Uint8Array(hashed)))
    .replace(/\+/g, '-')
    .replace(/\//g, '_')
    .replace(/=+$/, '');
};

// Message sending functions
SpotifyAuth.prototype.sendAuthSuccess = function() {
  console.log('sendAuthSuccess called');
  console.log('Access token:', this.accessToken);
  console.log('Refresh token:', this.refreshToken);
  console.log('Token expires at:', this.tokenExpiresAt);
  
  Pebble.sendAppMessage({
    1: 1, // AUTH_SUCCESS key
    7: this.accessToken, // ACCESS_TOKEN key
    8: this.refreshToken, // REFRESH_TOKEN key
    9: this.tokenExpiresAt // EXPIRES_AT key
  });
  
  console.log('Auth success message sent to C app');
};

SpotifyAuth.prototype.sendAuthError = function(error) {
  Pebble.sendAppMessage({
    2: 1, // AUTH_ERROR key
    error: error
  });
};

SpotifyAuth.prototype.sendApiResponse = function(data) {
  Pebble.sendAppMessage({
    5: 1, // API_RESPONSE key
    14: JSON.stringify(data) // RESPONSE_DATA key
  });
};

SpotifyAuth.prototype.sendParsedNowPlayingData = function(data) {
  console.log('Parsing now playing data:', data);
  
  // Parse the Spotify API response
  var trackName = '';
  var artistName = '';
  var isPlaying = false;
  var volumePercent = 50;
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
    if (data.device && data.device.volume_percent !== undefined) {
      volumePercent = data.device.volume_percent;
    }
    if (data.actions && data.actions.disallows) {
      canSkipPrev = !data.actions.disallows.skipping_prev;
      canSkipNext = !data.actions.disallows.skipping_next;
    }
  }
  
  console.log('Parsed data:', {
    trackName: trackName,
    artistName: artistName,
    isPlaying: isPlaying,
    volumePercent: volumePercent,
    canSkipPrev: canSkipPrev,
    canSkipNext: canSkipNext
  });
  
  // Send parsed data to C app
  Pebble.sendAppMessage({
    5: 1, // API_RESPONSE key
    15: trackName, // TRACK_NAME key
    16: artistName, // ARTIST_NAME key
    17: isPlaying ? 1 : 0, // IS_PLAYING key
    18: volumePercent, // VOLUME_PERCENT key
    19: canSkipPrev ? 1 : 0, // CAN_SKIP_PREV key
    20: canSkipNext ? 1 : 0 // CAN_SKIP_NEXT key
  });
};

SpotifyAuth.prototype.sendApiError = function(error) {
  Pebble.sendAppMessage({
    6: 1, // API_ERROR key
    13: error // ERROR_MESSAGE key
  });
};

// Load stored tokens on startup
SpotifyAuth.prototype.loadStoredTokens = function() {
  var userTokensStr = localStorage.getItem('userTokens');
  if (userTokensStr) {
    try {
      var userTokens = JSON.parse(userTokensStr);
      this.accessToken = userTokens.access_token;
      this.refreshToken = userTokens.refresh_token;
      this.tokenExpiresAt = userTokens.expiration_date;
    } catch (e) {
      console.error('Failed to parse stored tokens:', e);
    }
  }
};

// Initialize authentication when app starts
Pebble.addEventListener('ready', function() {
  console.log('Pebble ready event fired');
  var auth = new SpotifyAuth();
  auth.loadStoredTokens();
  console.log('Auth initialized');
});
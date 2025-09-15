// Spotify Authentication for Pebblify C App
// Use native Pebble API instead of PebbleJS to avoid module loading issues
var axios = require('axios');
var Settings = require('pebblejs/settings');
var sha256 = require('./sha256');
var constants = require('./constants');
var messageKeys = require('message_keys');

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
  
  // Volume control state
  this.pendingVolumeChange = null;
  this.volumeChangeTimer = null;
  this.currentVolume = null; // Will be set from API
  this.localVolume = null; // Local volume tracker (independent of API responses)
  this.lastVolumeApiCall = 0; // Timestamp of last volume API call
  this.lastVolumeChange = 0; // Timestamp of last volume change we made
  
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
  // console.log('initSettingsPage called');
  var self = this;
  this.authUrl = this.getAuthorizationUrl();
  // console.log('Authorization URL:', this.authUrl);
  
  // Use native Pebble Settings API
  Settings.config({
    url: this.authUrl,
    autosave: false,
    hash: true,
  }, function(e) {
    // console.log('opening configurable');
  }, function(e) {
    // console.log('Settings callback received:', e);
    if (e.options.hasOwnProperty('/?code')) {
      // user accepted authorization, code received
      var pkceCode = e.options['/?code'];
      // console.log('Authorization code received:', pkceCode);
      self.getToken(pkceCode);
    } else if (e.options.hasOwnProperty('/?error')) {
      // user closed authorization url
      // console.log('User closed Spotify authorize url');
    }
    if (e.failed) {
      // console.log('PARSING FAILED - Response:');
      // console.log(e.response);
    }
  });
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
  // console.log('getToken called with code:', pkceCode);
  var self = this;
  var codeVerifier = localStorage.getItem('pkceCodeVerifier');
  // console.log('Using code verifier:', codeVerifier);
  var body = 'client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&code_verifier=' + codeVerifier + '&code=' + pkceCode + '&grant_type=authorization_code';

  // console.log('Making token request to:', ACCOUNTS_BASE_URL + '/api/token');
  axios.post(ACCOUNTS_BASE_URL + '/api/token', body, {
    headers: {
      'content-type': 'application/x-www-form-urlencoded',
    },
  }).then(function(response) {
    // console.log('Token response received:', response.data);
    var userTokens = response.data;
    userTokens.expiration_date = Date.now() + userTokens.expires_in * 1000;

    localStorage.setItem('userTokens', JSON.stringify(userTokens));
    self.accessToken = userTokens.access_token;
    self.refreshToken = userTokens.refresh_token;
    self.tokenExpiresAt = userTokens.expiration_date;
    
    // console.log('Tokens stored, calling sendAuthSuccess');
    self.sendAuthSuccess();
  }).catch(function(data) {
    // console.log('Token request failed:', data);
    if (data.error == 'invalid_grant') {
      // Authorization code expired
      console.log('User must relaunch Pebblify settings app');
    }
  });
};

SpotifyAuth.prototype.handleAuthRequest = function() {
  console.log('handleAuthRequest called');
  
  // First, try to load tokens from localStorage
  this.loadStoredTokens();
  
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
  
  // Check if this is a volume change request
  if (apiPath && apiPath.includes('/me/player/volume')) {
    self.handleVolumeChange(apiPath, httpMethod);
    return;
  }
  
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
      // Retry the API call
      setTimeout(function() {
        self.handleApiCall(message);
      }, constants.TIMER_INTERVALS.API_RETRY_DELAY);
    } else {
      self.sendApiError(error.message);
    }
  });
};

// Volume control with accumulation
SpotifyAuth.prototype.handleVolumeChange = function(apiPath, httpMethod) {
  var self = this;
  
  // Extract volume percentage from the API path
  var volumeMatch = apiPath.match(/volume_percent=(\d+)/);
  if (!volumeMatch) {
    console.error('Could not extract volume from path:', apiPath);
    this.sendApiError('Invalid volume request');
    return;
  }
  
  var targetVolume = parseInt(volumeMatch[1]);
  
  // If we don't know the current volume, get it from API first
  if (this.localVolume === null) {
    console.log('Unknown volume, fetching from API first');
    this.getCurrentVolumeFromAPI(function(currentVolume) {
      self.localVolume = currentVolume;
      self.handleVolumeChangeRequest(targetVolume);
    });
    return;
  }
  
  this.handleVolumeChangeRequest(targetVolume);
};

SpotifyAuth.prototype.getCurrentVolumeFromAPI = function(callback) {
  var self = this;
  console.log('Fetching current volume from API');
  
  axios.get(API_BASE_URL + '/me/player', {
    headers: {
      'Authorization': 'Bearer ' + this.accessToken,
      'Content-Type': 'application/json'
    }
  }).then(function(response) {
    try {
      var data = JSON.parse(response);
      if (data && data.device && data.device.volume_percent !== undefined) {
        var volume = data.device.volume_percent;
        console.log('Got current volume from API:', volume);
        callback(volume);
      } else {
        console.log('No volume data in API response, using default 50');
        callback(constants.VOLUME_CONFIG.DEFAULT);
      }
    } catch (e) {
      console.error('Failed to parse volume API response:', e);
      callback(constants.VOLUME_CONFIG.DEFAULT);
    }
  }).catch(function(error) {
    console.error('Failed to get current volume:', error);
    callback(constants.VOLUME_CONFIG.DEFAULT);
  });
};

SpotifyAuth.prototype.handleVolumeChangeRequest = function(targetVolume) {
  var self = this;
  
  console.log('Volume change request: local=' + this.localVolume + ', target=' + targetVolume);
  
  // If there's already a pending volume change, accumulate it
  if (this.pendingVolumeChange) {
    console.log('Accumulating volume change: existing target=' + this.pendingVolumeChange.targetVolume + ', new target=' + targetVolume);
    
    // Cancel the existing timer
    if (this.volumeChangeTimer) {
      clearTimeout(this.volumeChangeTimer);
    }
    
    // Calculate the total delta from the original volume (when the first press happened)
    var totalDelta = targetVolume - this.pendingVolumeChange.originalVolume;
    var newTargetVolume = this.pendingVolumeChange.originalVolume + totalDelta;
    
    // Clamp to valid range
    if (newTargetVolume < constants.VOLUME_CONFIG.MIN) newTargetVolume = constants.VOLUME_CONFIG.MIN;
    if (newTargetVolume > constants.VOLUME_CONFIG.MAX) newTargetVolume = constants.VOLUME_CONFIG.MAX;
    
    this.pendingVolumeChange.targetVolume = newTargetVolume;
    this.pendingVolumeChange.volumeDelta = totalDelta; // This delta is now the total delta from original
    
    console.log('Accumulated volume change: total delta=' + totalDelta + ', final target=' + newTargetVolume);
  } else {
    // Start a new volume change
    var delta = targetVolume - this.localVolume; // Initial delta from current local volume
    this.pendingVolumeChange = {
      targetVolume: targetVolume,
      originalVolume: this.localVolume, // Store current volume as original
      volumeDelta: delta
    };
    console.log('Starting new volume change: delta=' + delta);
  }
  
  this.volumeChangeTimer = setTimeout(function() {
    self.executeVolumeChange();
  }, constants.TIMER_INTERVALS.VOLUME_CHANGE_DELAY); // 150ms delay to allow for rapid presses
};

SpotifyAuth.prototype.executeVolumeChange = function() {
  var self = this;
  
  if (!this.pendingVolumeChange) {
    return;
  }
  
  var targetVolume = this.pendingVolumeChange.targetVolume;
  var volumeDelta = this.pendingVolumeChange.volumeDelta;
  var currentTime = Date.now();
  
  console.log('Executing volume change: target=' + targetVolume + ', delta=' + volumeDelta);
  
  // Check rate limit - only allow API calls every 300ms
  if (currentTime - this.lastVolumeApiCall < constants.TIMER_INTERVALS.VOLUME_RATE_LIMIT) {
    var waitTime = constants.TIMER_INTERVALS.VOLUME_RATE_LIMIT - (currentTime - this.lastVolumeApiCall);
    console.log('Rate limiting: waiting ' + waitTime + 'ms before API call');
    
    // Reschedule the execution after the rate limit period
    setTimeout(function() {
      self.executeVolumeChange();
    }, waitTime);
    return;
  }
  
  // Update our local volume tracker immediately
  this.localVolume = targetVolume;
  
  // Record the API call timestamp and volume change timestamp
  this.lastVolumeApiCall = currentTime;
  this.lastVolumeChange = currentTime;
  
  // Make the API call
  axios.put(API_BASE_URL + '/me/player/volume?volume_percent=' + targetVolume, null, {
    headers: {
      'Authorization': 'Bearer ' + this.accessToken,
      'Content-Type': 'application/json'
    }
  }).then(function(response) {
    console.log('Volume change successful: ' + targetVolume + '%');
    
    // Update our current volume (for API responses)
    self.currentVolume = targetVolume;
    
    // Clear pending change
    self.pendingVolumeChange = null;
    self.volumeChangeTimer = null;
    
    // Send success response back to C app
    self.sendParsedNowPlayingData({
      device: {
        volume_percent: targetVolume
      }
    });
  }).catch(function(error) {
    console.error('Volume change failed:', error);
    
    // Clear pending change
    self.pendingVolumeChange = null;
    self.volumeChangeTimer = null;
    
    // Send error back to C app
    self.sendApiError('Volume change failed: ' + error.message);
  });
};

// Utility functions
SpotifyAuth.prototype.generateRandomString = function(length) {
  var charset = constants.PKCE_CONFIG.CHARSET;
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
  // console.log('JS: Sending AUTH_SUCCESS message to C app');
  console.log('sendAuthSuccess called');
  console.log('Access token length:', this.accessToken ? this.accessToken.length : 0);
  console.log('Refresh token length:', this.refreshToken ? this.refreshToken.length : 0);
  console.log('Token expires at:', this.tokenExpiresAt);
  
  Pebble.sendAppMessage({
    [messageKeys.AUTH_SUCCESS]: 1,
    [messageKeys.ACCESS_TOKEN]: this.accessToken,
    [messageKeys.REFRESH_TOKEN]: this.refreshToken,
    [messageKeys.EXPIRES_AT]: this.tokenExpiresAt
  });
  
  // Start now playing polling if available
  if (typeof require !== 'undefined') {
    try {
      var NowPlayingManager = require('./now_playing.js');
      NowPlayingManager.setAccessToken(this.accessToken);
      NowPlayingManager.startPolling();
      console.log('Now playing polling started');
    } catch (e) {
      console.log('Now playing manager not available:', e);
    }
  }
  
  // console.log('Auth success message sent to C app');
};

SpotifyAuth.prototype.sendAuthError = function(error) {
  Pebble.sendAppMessage({
    [messageKeys.AUTH_ERROR]: 1,
    error: error
  });
};

SpotifyAuth.prototype.sendApiResponse = function(data) {
  Pebble.sendAppMessage({
    [messageKeys.API_RESPONSE]: 1,
    [messageKeys.RESPONSE_DATA]: JSON.stringify(data)
  });
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
      
      // Check if this API response is stale (came too soon after our volume change)
      var currentTime = Date.now();
      var timeSinceLastChange = currentTime - this.lastVolumeChange;
      
      console.log('API volume check: API=' + volumePercent + ', local=' + this.localVolume + ', timeSince=' + timeSinceLastChange + 'ms');
      
      if (timeSinceLastChange < constants.TIMER_INTERVALS.VOLUME_STALE_GRACE) { // 2 seconds grace period
        console.log('Ignoring stale API volume update:', volumePercent, '(local:', this.localVolume + ')');
        // Use our local volume instead
        volumePercent = this.localVolume;
      } else {
        // Update local volume from API - this ensures we stay in sync
        this.currentVolume = volumePercent;
        this.localVolume = volumePercent;
        console.log('Updated volume from API:', volumePercent);
      }
      // console.log('Volume from API:', data.device.volume_percent, 'Type:', typeof data.device.volume_percent);
    } else {
      // console.log('No device volume_percent found, using default:', volumePercent);
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
  
  var messageToSend = {
    [messageKeys.API_RESPONSE]: 1,
    [messageKeys.TRACK_NAME]: trackName,
    [messageKeys.ARTIST_NAME]: artistName,
    [messageKeys.IS_PLAYING]: isPlaying ? 1 : 0,
    [messageKeys.VOLUME_PERCENT]: volumePercent,
    [messageKeys.CAN_SKIP_PREV]: canSkipPrev ? 1 : 0,
    [messageKeys.CAN_SKIP_NEXT]: canSkipNext ? 1 : 0
  };
  
  // console.log('Sending message to C app:', messageToSend);
  // console.log('Volume value being sent:', volumePercent, 'Type:', typeof volumePercent);
  
  // Send parsed data to C app
  Pebble.sendAppMessage(messageToSend);
};

SpotifyAuth.prototype.sendApiError = function(error) {
  Pebble.sendAppMessage({
    [messageKeys.API_ERROR]: 1,
    [messageKeys.ERROR_MESSAGE]: error
  });
};

// Load stored tokens on startup
SpotifyAuth.prototype.loadStoredTokens = function() {
  var userTokensStr = localStorage.getItem('userTokens');
  if (userTokensStr) {
    try {
      var userTokens = JSON.parse(userTokensStr);
      var newAccessToken = userTokens.access_token;
      var newRefreshToken = userTokens.refresh_token;
      var newTokenExpiresAt = userTokens.expiration_date;
      
      console.log('Loaded tokens from localStorage');
      console.log('Token expires at:', newTokenExpiresAt);
      console.log('Current time:', Date.now());
      console.log('Token valid:', Date.now() < newTokenExpiresAt);
      
      // Check if tokens have changed
      var tokensChanged = false;
      if (this.accessToken !== newAccessToken) {
        this.accessToken = newAccessToken;
        tokensChanged = true;
        console.log('Access token changed');
      } else {
        console.log('Access token unchanged');
      }
      
      if (this.refreshToken !== newRefreshToken) {
        this.refreshToken = newRefreshToken;
        tokensChanged = true;
        console.log('Refresh token changed');
      } else {
        console.log('Refresh token unchanged');
      }
      
      if (this.tokenExpiresAt !== newTokenExpiresAt) {
        this.tokenExpiresAt = newTokenExpiresAt;
        tokensChanged = true;
        console.log('Token expiration changed');
      } else {
        console.log('Token expiration unchanged');
      }
      
      // If tokens are valid, send them to C app (only if they changed or this is first load)
      if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
        if (tokensChanged || !this.accessToken) {
          console.log('Valid tokens found, sending to C app');
          this.sendAuthSuccess();
        } else {
          console.log('Valid tokens found but unchanged, skipping C app notification');
        }
      }
    } catch (e) {
      console.error('Failed to parse stored tokens:', e);
    }
  } else {
    console.log('No tokens found in localStorage');
  }
};

// Initialize authentication when app starts
Pebble.addEventListener('ready', function() {
  // console.log('Pebble ready event fired');
  var auth = new SpotifyAuth();
  // console.log('Auth initialized');
});

// Export for module use
module.exports = SpotifyAuth;
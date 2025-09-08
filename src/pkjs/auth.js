// Spotify Authentication for Pebblify C App
const axios = require('axios');
require('pebblejs');
var Settings = require('pebblejs/settings');

// Spotify API constants
const CLIENT_ID = '152d31f9089d4be0b6605671dae99c3f';
const PEBBLE_REDIRECT_URI = 'pebblejs://close';
const SCOPES = [
  'user-read-private',
  'user-read-email',
  'user-read-playback-state',
  'user-modify-playback-state',
  'user-read-currently-playing',
  'playlist-read-private',
  'playlist-read-collaborative',
];
const ACCOUNTS_BASE_URL = 'https://accounts.spotify.com';
const API_BASE_URL = 'https://api.spotify.com/v1';

// Message keys for communication with C app
const MESSAGE_KEYS = {
  AUTH_REQUEST: 0,
  AUTH_SUCCESS: 1,
  AUTH_ERROR: 2,
  TOKEN_REFRESH: 3,
  API_CALL: 4,
  API_RESPONSE: 5,
  API_ERROR: 6
};

class SpotifyAuth {
  constructor() {
    console.log('SpotifyAuth constructor called');
    this.accessToken = null;
    this.refreshToken = null;
    this.tokenExpiresAt = null;
    this.setupAppMessageHandlers();
    this.initSettingsPage();
    console.log('SpotifyAuth constructor completed');
  }

  setupAppMessageHandlers() {
    // Handle authentication requests from C app
    Pebble.addEventListener('appmessage', (e) => {
      const message = e.payload;
      
      switch (message.message_type) {
        case MESSAGE_KEYS.AUTH_REQUEST:
          this.handleAuthRequest();
          break;
        case MESSAGE_KEYS.TOKEN_REFRESH:
          this.refreshAccessToken();
          break;
        case MESSAGE_KEYS.API_CALL:
          this.handleApiCall(message);
          break;
      }
    });
  }

  initSettingsPage() {
    console.log('initSettingsPage called');
    var self = this;
    var authUrl = this.getAuthorizationUrl();
    console.log('Authorization URL:', authUrl);
    
    Settings.config({
      url: authUrl,
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
  }

  getAuthorizationUrl() {
    // Create and store a random "state" value
    var state = this.generateRandomString(16);
    localStorage.setItem('pkceState', state);

    // Create and store a new PKCE code_verifier (the plaintext random secret)
    var codeVerifier = this.generateRandomString(128);
    localStorage.setItem('pkceCodeVerifier', codeVerifier);

    // Hash and base64-urlencode the secret to use as the challenge
    var codeChallenge = this.pkceChallengeFromVerifier(codeVerifier);

    return ACCOUNTS_BASE_URL + '/authorize?client_id=' + CLIENT_ID + '&redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&scope=' + encodeURIComponent(SCOPES.join(' ')) + '&code_challenge=' + codeChallenge + '&code_challenge_method=S256&state=123&response_type=code';
  }

  getToken(pkceCode) {
    var self = this;
    var codeVerifier = localStorage.getItem('pkceCodeVerifier');
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
      
      self.sendAuthSuccess();
    }).catch(function(data) {
      if (data.error == 'invalid_grant') {
        // Authorization code expired
        console.log('User must relaunch Pebblify settings app');
      }
    });
  }

  handleAuthRequest() {
    console.log('handleAuthRequest called');
    
    // Check if we already have valid tokens
    if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
      console.log('Already authenticated, sending success');
      this.sendAuthSuccess();
      return;
    }

    console.log('Opening settings for authentication');
    // Open settings page for authentication
    Settings.open();
  }


  refreshAccessToken() {
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
  }

  handleApiCall(message) {
    var self = this;
    if (!this.accessToken) {
      this.sendApiError('No access token available');
      return;
    }

    axios({
      url: API_BASE_URL + message.api_path,
      method: message.http_method || 'GET',
      headers: {
        'Authorization': 'Bearer ' + this.accessToken,
        'Content-Type': 'application/json'
      },
      data: message.data || {}
    }).then(function(response) {
      self.sendApiResponse(response.data);
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
  }

  // Utility functions
  generateRandomString(length) {
    var charset = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~';
    var result = '';
    for (var i = 0; i < length; i++) {
      result += charset.charAt(Math.floor(Math.random() * charset.length));
    }
    return result;
  }

  pkceChallengeFromVerifier(verifier) {
    // Simple SHA256 implementation for PKCE
    var crypto = require('crypto');
    var hash = crypto.createHash('sha256').update(verifier).digest('base64');
    return hash.replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '');
  }

  // Message sending functions
  sendAuthSuccess() {
    Pebble.sendAppMessage({
      message_type: MESSAGE_KEYS.AUTH_SUCCESS,
      access_token: this.accessToken,
      refresh_token: this.refreshToken,
      expires_at: this.tokenExpiresAt
    });
  }

  sendAuthError(error) {
    Pebble.sendAppMessage({
      message_type: MESSAGE_KEYS.AUTH_ERROR,
      error: error
    });
  }

  sendApiResponse(data) {
    Pebble.sendAppMessage({
      message_type: MESSAGE_KEYS.API_RESPONSE,
      data: JSON.stringify(data)
    });
  }

  sendApiError(error) {
    Pebble.sendAppMessage({
      message_type: MESSAGE_KEYS.API_ERROR,
      error: error
    });
  }

  // Load stored tokens on startup
  loadStoredTokens() {
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
  }
}

// Initialize authentication when app starts
Pebble.addEventListener('ready', function() {
  console.log('Pebble ready event fired');
  var auth = new SpotifyAuth();
  auth.loadStoredTokens();
  console.log('Auth initialized');
});


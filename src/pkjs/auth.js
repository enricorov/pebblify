// Spotify Authentication for Pebblify C App
const axios = require('axios');

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
    this.accessToken = null;
    this.refreshToken = null;
    this.tokenExpiresAt = null;
    this.setupAppMessageHandlers();
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

  handleAuthRequest() {
    // Check if we already have valid tokens
    if (this.accessToken && this.tokenExpiresAt && Date.now() < this.tokenExpiresAt) {
      this.sendAuthSuccess();
      return;
    }

    // Generate PKCE parameters
    const codeVerifier = this.generateRandomString(128);
    const codeChallenge = this.pkceChallengeFromVerifier(codeVerifier);
    const state = this.generateRandomString(16);

    // Store PKCE parameters
    localStorage.setItem('pkce_code_verifier', codeVerifier);
    localStorage.setItem('pkce_state', state);

    // Build authorization URL
    var authUrl = ACCOUNTS_BASE_URL + '/authorize?' +
      'client_id=' + CLIENT_ID + '&' +
      'redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&' +
      'scope=' + encodeURIComponent(SCOPES.join(' ')) + '&' +
      'code_challenge=' + codeChallenge + '&' +
      'code_challenge_method=S256&' +
      'state=' + state + '&' +
      'response_type=code';

    // Open authorization URL
    Pebble.openURL(authUrl);
  }

  handleAuthCallback(url) {
    const urlParams = new URLSearchParams(url.split('?')[1]);
    const code = urlParams.get('code');
    const state = urlParams.get('state');
    const error = urlParams.get('error');

    if (error) {
      this.sendAuthError(error);
      return;
    }

    if (!code || state !== localStorage.getItem('pkce_state')) {
      this.sendAuthError('Invalid authorization response');
      return;
    }

    // Exchange code for tokens
    this.exchangeCodeForTokens(code);
  }

  exchangeCodeForTokens(code) {
    var self = this;
    var codeVerifier = localStorage.getItem('pkce_code_verifier');
    
    axios.post(ACCOUNTS_BASE_URL + '/api/token', 
      'client_id=' + CLIENT_ID + '&' +
      'redirect_uri=' + encodeURIComponent(PEBBLE_REDIRECT_URI) + '&' +
      'code_verifier=' + codeVerifier + '&' +
      'code=' + code + '&' +
      'grant_type=authorization_code',
      {
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded'
        }
      }
    ).then(function(response) {
      var tokens = response.data;
      self.accessToken = tokens.access_token;
      self.refreshToken = tokens.refresh_token;
      self.tokenExpiresAt = Date.now() + (tokens.expires_in * 1000);

      // Store tokens
      localStorage.setItem('spotify_access_token', self.accessToken);
      localStorage.setItem('spotify_refresh_token', self.refreshToken);
      localStorage.setItem('spotify_token_expires_at', self.tokenExpiresAt);

      self.sendAuthSuccess();
    }).catch(function(error) {
      console.error('Token exchange failed:', error);
      self.sendAuthError('Failed to exchange authorization code');
    });
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
    this.accessToken = localStorage.getItem('spotify_access_token');
    this.refreshToken = localStorage.getItem('spotify_refresh_token');
    this.tokenExpiresAt = parseInt(localStorage.getItem('spotify_token_expires_at'));
  }
}

// Initialize authentication when app starts
Pebble.addEventListener('ready', () => {
  const auth = new SpotifyAuth();
  auth.loadStoredTokens();
  
  // Handle URL callbacks from OAuth flow
  Pebble.addEventListener('webviewclosed', (e) => {
    if (e.url) {
      auth.handleAuthCallback(e.url);
    }
  });
});


// Pebblify Constants
// Centralized configuration for the Pebblify Spotify app

// ============================================================================
// Spotify API Configuration
// ============================================================================
var SPOTIFY_CONFIG = {
  CLIENT_ID: '152d31f9089d4be0b6605671dae99c3f',
  PEBBLE_REDIRECT_URI: 'pebblejs://close',
  SCOPES: [
    'user-read-private',
    'user-read-email',
    'user-read-playback-state',
    'user-modify-playback-state',
    'user-read-currently-playing',
    'playlist-read-private',
    'playlist-read-collaborative',
  ],
  ACCOUNTS_BASE_URL: 'https://accounts.spotify.com',
  API_BASE_URL: 'https://api.spotify.com/v1'
};

// ============================================================================
// Message Keys for C ↔ JavaScript Communication
// ============================================================================
// Message keys are now defined in package.json and auto-generated
// They are available as global variables in the webpack build

// ============================================================================
// Timer Intervals (in milliseconds)
// ============================================================================
var TIMER_INTERVALS = {
  REFRESH_INTERVAL: 10000,        // Now playing refresh interval
  CLOCK_UPDATE_INTERVAL: 60000,    // Clock update interval
  VOLUME_FETCH_INTERVAL: 2000,     // Volume fetch cooldown
  VOLUME_ERROR_DISPLAY: 1000,      // Volume error display duration
  ACTION_FEEDBACK_DELAY: 700,     // Action feedback delay
  API_RETRY_DELAY: 1000,           // API retry delay
  VOLUME_CHANGE_DELAY: 150,        // Volume change accumulation delay
  VOLUME_RATE_LIMIT: 300,          // Volume API rate limit
  VOLUME_STALE_GRACE: 2000         // Volume stale data grace period
};

// ============================================================================
// Volume Control Constants
// ============================================================================
var VOLUME_CONFIG = {
  STEP_SIZE: 3,                    // Volume change per button press
  MIN: 0,                         // Minimum volume
  MAX: 100,                       // Maximum volume
  DEFAULT: 50,                    // Default volume
  SUSPICIOUS_THRESHOLD: 15,       // Threshold for suspicious volume jumps
  PENDING_THRESHOLD: 10           // Threshold during pending changes
};

// ============================================================================
// PKCE Configuration
// ============================================================================
var PKCE_CONFIG = {
  STATE_LENGTH: 16,               // Random state string length
  CODE_VERIFIER_LENGTH: 128,      // PKCE code verifier length
  CHARSET: 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~'
};

// ============================================================================
// Default Text Strings
// ============================================================================
var DEFAULT_TEXTS = {
  NO_SESSION: "No active session",
  NO_ACTIVE_SESSION: "No Active Session",
  START_MUSIC: "Start playing music on Spotify",
  NEXT_TRACK: "Next Track",
  PREV_TRACK: "Previous Track",
  AUTH_EXPIRED: "Authentication expired",
  REAUTH: "Please re-authenticate",
  NO_DEVICE: "No Active Device",
  CONNECTION_ERROR: "Connection error",
  CHECK_CONNECTION: "Check your internet connection",
  APP_NAME: "Pebblify",
  CONNECT: "Connect to Spotify",
  AUTH_INSTRUCTION: "Press SELECT to authorize\nwith Spotify"
};

// ============================================================================
// HTTP Status Codes
// ============================================================================
var HTTP_STATUS = {
  UNAUTHORIZED: 401,
  FORBIDDEN: 403,
  NOT_FOUND: 404,
  TOO_MANY_REQUESTS: 429
};

// ============================================================================
// Menu Configuration
// ============================================================================
var MENU_CONFIG = {
  SECTIONS: 3,
  HOME_ROWS: 3,
  LIBRARY_ROWS: 3,
  DEVICES_ROWS: 1,
  HEADERS: ["Home", "Library", "Devices"],
  HOME_ITEMS: ["Now playing", "Jump back in", "Made for you"],
  LIBRARY_ITEMS: ["Playlists", "Albums", "Artists"],
  DEVICES_ITEMS: ["Play on device", "", ""]
};

// ============================================================================
// API Endpoints
// ============================================================================
var API_ENDPOINTS = {
  AUTHORIZE: '/authorize',
  TOKEN: '/api/token',
  PLAYER: '/me/player',
  PLAY: '/me/player/play',
  PAUSE: '/me/player/pause',
  NEXT: '/me/player/next',
  PREVIOUS: '/me/player/previous',
  VOLUME: '/me/player/volume'
};

// ============================================================================
// Error Messages
// ============================================================================
var ERROR_MESSAGES = {
  NO_ACCESS_TOKEN: "No access token received",
  NO_REFRESH_TOKEN: "No refresh token received",
  NO_EXPIRATION_TIME: "No expiration time received",
  TOKEN_EXPIRED: "Token expired",
  API_CALL_FAILED: "API call failed",
  VOLUME_API_FAILED: "Failed to get current volume",
  PARSE_ERROR: "Failed to parse volume API response"
};

// ============================================================================
// Debug Configuration
// ============================================================================
var DEBUG_CONFIG = {
  ENABLE_LOGGING: false,          // Set to true to enable debug logging
  LOG_PREFIX: "Pebblify:"
};

// ============================================================================
// Module Exports
// ============================================================================
module.exports = {
  SPOTIFY_CONFIG: SPOTIFY_CONFIG,
  TIMER_INTERVALS: TIMER_INTERVALS,
  VOLUME_CONFIG: VOLUME_CONFIG,
  PKCE_CONFIG: PKCE_CONFIG,
  DEFAULT_TEXTS: DEFAULT_TEXTS,
  HTTP_STATUS: HTTP_STATUS,
  MENU_CONFIG: MENU_CONFIG,
  API_ENDPOINTS: API_ENDPOINTS,
  ERROR_MESSAGES: ERROR_MESSAGES,
  DEBUG_CONFIG: DEBUG_CONFIG
};


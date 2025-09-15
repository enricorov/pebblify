/**
 * Now Playing Module for Pebblify
 * 
 * This module handles the JavaScript side of the now playing functionality:
 * - Polls Spotify API every second for current track information
 * - Caches and compares data to detect changes
 * - Sends updated track data to C side via AppMessage
 * - Processes action commands from C side (play/pause, skip, volume)
 * - Implements intelligent volume accumulation for rapid button presses
 * 
 * Communication Flow:
 * C → JS: Action commands (ACTION key with string values)
 * JS → C: Track data updates (TRACK_NAME, ARTIST_NAME, etc.)
 * JS → C: Error messages (ERROR_MESSAGE)
 */

var axios = require('axios');
var constants = require('./constants');
var messageKeys = require('message_keys');

// ============================================================================
// Module State Management
// ============================================================================

var nowPlayingState = {
  // Polling control
  isPolling: false,
  pollingTimer: null,
  accessToken: null,
  
  // Simple volume control
  cachedVolume: null,             // Current cached volume (null until initialized)
  volumeChangeTimer: null,        // Timer for sending accumulated volume changes
  
  // Cached track data for change detection
  cachedData: {
    trackName: null,
    artistName: null,
    isPlaying: false,
    volumePercent: 50,
    canSkipPrev: true,
    canSkipNext: true
  }
};

// ============================================================================
// Main NowPlayingManager Class
// ============================================================================

function NowPlayingManager() {
  this.setupAppMessageHandlers();
}

// ============================================================================
// AppMessage Communication (C ↔ JS)
// ============================================================================

/**
 * Sets up handlers for messages from C side
 * C sends action commands using single ACTION key with string values
 */
NowPlayingManager.prototype.setupAppMessageHandlers = function() {
  var self = this;
  
  Pebble.addEventListener('appmessage', function(e) {
    var message = e.payload;
    
    // Handle action commands from C
    if (message['ACTION'] !== undefined) {
      var action = message['ACTION'];
      
      switch (action) {
        case 'skip_next':
          self.handleSkipNext();
          break;
        case 'skip_prev':
          self.handleSkipPrev();
          break;
        case 'play_pause':
          self.handlePlayPause();
          break;
        case 'volume_up':
          self.handleVolumeUp();
          break;
        case 'volume_down':
          self.handleVolumeDown();
          break;
      }
    }
    // Handle polling control from C
    else if (message['START_POLLING'] !== undefined) {
      self.startPolling();
    }
    else if (message['STOP_POLLING'] !== undefined) {
      self.stopPolling();
    }
  });
};

/**
 * Sends track data to C side when changes are detected
 */
NowPlayingManager.prototype.sendNowPlayingData = function(data) {
  var message = {
    [messageKeys.API_RESPONSE]: 1,
    [messageKeys.TRACK_NAME]: data.trackName,
    [messageKeys.ARTIST_NAME]: data.artistName,
    [messageKeys.IS_PLAYING]: data.isPlaying ? 1 : 0,
    [messageKeys.VOLUME_PERCENT]: data.volumePercent,
    [messageKeys.CAN_SKIP_PREV]: data.canSkipPrev ? 1 : 0,
    [messageKeys.CAN_SKIP_NEXT]: data.canSkipNext ? 1 : 0
  };
  
  Pebble.sendAppMessage(message, function() {
    // Success callback - data sent to C
  }, function(error) {
    console.error('Failed to send track data to C:', error);
  });
};

/**
 * Sends error messages to C side
 */
NowPlayingManager.prototype.sendError = function(errorMessage) {
  var message = {
    [messageKeys.API_ERROR]: 1,
    [messageKeys.ERROR_MESSAGE]: errorMessage
  };
  
  Pebble.sendAppMessage(message, function() {
    // Error message sent to C
  }, function(error) {
    console.error('Failed to send error to C:', error);
  });
};

// ============================================================================
// Polling and Data Management
// ============================================================================

/**
 * Sets the Spotify access token for API calls
 */
NowPlayingManager.prototype.setAccessToken = function(token) {
  nowPlayingState.accessToken = token;
};

/**
 * Starts polling Spotify API for track information
 * Called by C when now playing window is opened
 */
NowPlayingManager.prototype.startPolling = function() {
  if (nowPlayingState.isPolling || !nowPlayingState.accessToken) {
    return;
  }
  
  nowPlayingState.isPolling = true;
  
  // Start polling immediately
  this.pollNowPlaying();
  
  // Set up recurring polling using one-shot timers
  this.scheduleNextPoll();
};

/**
 * Stops polling Spotify API
 * Called by C when now playing window is closed
 */
NowPlayingManager.prototype.stopPolling = function() {
  if (!nowPlayingState.isPolling) {
    return;
  }
  
  nowPlayingState.isPolling = false;
  
  if (nowPlayingState.pollingTimer) {
    clearTimeout(nowPlayingState.pollingTimer);
    nowPlayingState.pollingTimer = null;
  }
};

/**
 * Schedules the next polling cycle using a one-shot timer
 */
NowPlayingManager.prototype.scheduleNextPoll = function() {
  var self = this;
  
  if (!nowPlayingState.isPolling) {
    return;
  }
  
  nowPlayingState.pollingTimer = setTimeout(function() {
    self.pollNowPlaying();
    self.scheduleNextPoll(); // Schedule the next poll
  }, constants.TIMER_INTERVALS.POLLING_INTERVAL);
};

/**
 * Polls Spotify API for current track information
 */
NowPlayingManager.prototype.pollNowPlaying = function() {
  if (!nowPlayingState.accessToken) {
    return;
  }
  
  var self = this;
  
  axios({
    url: constants.SPOTIFY_CONFIG.API_BASE_URL + '/me/player',
    method: 'GET',
    headers: {
      'Authorization': 'Bearer ' + nowPlayingState.accessToken,
      'Content-Type': 'application/json'
    }
  }).then(function(response) {
    self.handleNowPlayingResponse(response.data);
  }).catch(function(error) {
    if (error.response && error.response.status === constants.HTTP_STATUS.UNAUTHORIZED) {
      self.sendError('Authentication expired');
    } else if (error.response && error.response.status === constants.HTTP_STATUS.NOT_FOUND) {
      self.handleNoActiveSession();
    } else {
      self.sendError('Connection error');
    }
  });
};

/**
 * Processes API response and sends updates to C if data changed
 */
NowPlayingManager.prototype.handleNowPlayingResponse = function(data) {
  var newData = this.parseNowPlayingData(data);
  
  // Only send updates if data actually changed
  if (this.hasDataChanged(newData)) {
    // Update cache with new data
    nowPlayingState.cachedData = newData;
    
    // Send updated data to C
    this.sendNowPlayingData(newData);
  }
};

/**
 * Parses raw Spotify API response into standardized track data
 * Handles volume update conflicts during user-initiated volume changes
 */
NowPlayingManager.prototype.parseNowPlayingData = function(data) {
  var trackData = {
    trackName: 'No Active Session',
    artistName: 'Start playing music on Spotify',
    isPlaying: false,
    volumePercent: 50,
    canSkipPrev: true,
    canSkipNext: true
  };
  
  if (data && data.is_playing !== undefined) {
    trackData.isPlaying = data.is_playing;
    
    if (data.item && data.item.name) {
      trackData.trackName = data.item.name;
    }
    
    if (data.item && data.item.artists && data.item.artists.length > 0) {
      trackData.artistName = data.item.artists[0].name;
    }
    
    // Volume handling: use API volume unless we have a cached volume
    if (data.device && data.device.volume_percent !== undefined) {
      if (nowPlayingState.cachedVolume === null) {
        trackData.volumePercent = data.device.volume_percent;
      } else {
        // Use our cached volume during user changes
        trackData.volumePercent = nowPlayingState.cachedVolume;
      }
    }
    
    if (data.actions) {
      trackData.canSkipPrev = data.actions.disallows && !data.actions.disallows.skipping_prev;
      trackData.canSkipNext = data.actions.disallows && !data.actions.disallows.skipping_next;
    }
  }
  
  return trackData;
};

/**
 * Compares new data with cached data to detect changes
 */
NowPlayingManager.prototype.hasDataChanged = function(newData) {
  var cached = nowPlayingState.cachedData;
  
  return (
    newData.trackName !== cached.trackName ||
    newData.artistName !== cached.artistName ||
    newData.isPlaying !== cached.isPlaying ||
    newData.volumePercent !== cached.volumePercent ||
    newData.canSkipPrev !== cached.canSkipPrev ||
    newData.canSkipNext !== cached.canSkipNext
  );
};

/**
 * Handles case when no active Spotify session is found
 */
NowPlayingManager.prototype.handleNoActiveSession = function() {
  var noSessionData = {
    trackName: 'No Active Session',
    artistName: 'Start playing music on Spotify',
    isPlaying: false,
    volumePercent: 50,
    canSkipPrev: true,
    canSkipNext: true
  };
  
  if (this.hasDataChanged(noSessionData)) {
    nowPlayingState.cachedData = noSessionData;
    this.sendNowPlayingData(noSessionData);
  }
};

// ============================================================================
// Action Handlers (Called by C via AppMessage)
// ============================================================================

/**
 * Handles skip to next track command from C
 */
NowPlayingManager.prototype.handleSkipNext = function() {
  this.makeApiCall('/me/player/next', 'POST');
};

/**
 * Handles skip to previous track command from C
 */
NowPlayingManager.prototype.handleSkipPrev = function() {
  this.makeApiCall('/me/player/previous', 'POST');
};

/**
 * Handles play/pause toggle command from C
 */
NowPlayingManager.prototype.handlePlayPause = function() {
  var action = nowPlayingState.cachedData.isPlaying ? 'pause' : 'play';
  this.makeApiCall('/me/player/' + action, 'PUT');
};

/**
 * Handles volume up command from C
 */
NowPlayingManager.prototype.handleVolumeUp = function() {
  this.handleVolumeChange(1);
};

/**
 * Handles volume down command from C
 */
NowPlayingManager.prototype.handleVolumeDown = function() {
  this.handleVolumeChange(-1);
};

// ============================================================================
// Simple Volume Control System
// ============================================================================

/**
 * Implements simple volume change handling:
 * - First session: get initial volume reading from API
 * - Track cached volume changes locally
 * - Send accumulated changes every 300ms after first change
 */
NowPlayingManager.prototype.handleVolumeChange = function(direction) {
  var self = this;
  
  // If we haven't initialized volume yet, get it from API first
  if (nowPlayingState.cachedVolume === null) {
    console.log('Volume not initialized, getting initial reading from API');
    this.getInitialVolume(function(initialVolume) {
      nowPlayingState.cachedVolume = initialVolume;
      self.handleVolumeChange(direction); // Retry with initialized volume
    });
    return;
  }
  
  // Calculate new volume
  var stepSize = constants.VOLUME_CONFIG.STEP_SIZE;
  var newVolume = nowPlayingState.cachedVolume + (direction * stepSize);
  
  // Clamp to valid range
  newVolume = Math.max(constants.VOLUME_CONFIG.MIN, Math.min(constants.VOLUME_CONFIG.MAX, newVolume));
  
  // Update cached volume immediately
  nowPlayingState.cachedVolume = newVolume;
  
  // If this is the first change, start the timer
  if (!nowPlayingState.volumeChangeTimer) {
    console.log('Starting volume change timer, first change to:', newVolume);
    nowPlayingState.volumeChangeTimer = setTimeout(function() {
      self.sendAccumulatedVolumeChange();
    }, 300);
  } else {
    console.log('Accumulating volume change to:', newVolume);
  }
};

/**
 * Gets initial volume reading from API
 */
NowPlayingManager.prototype.getInitialVolume = function(callback) {
  var self = this;
  
  axios.get(constants.SPOTIFY_CONFIG.API_BASE_URL + '/me/player', {
    headers: {
      'Authorization': 'Bearer ' + nowPlayingState.accessToken,
      'Content-Type': 'application/json'
    }
  }).then(function(response) {
    var volume = constants.VOLUME_CONFIG.DEFAULT; // Default fallback
    
    if (response.data && response.data.device && response.data.device.volume_percent !== undefined) {
      volume = response.data.device.volume_percent;
    }
    
    console.log('Got initial volume from API:', volume);
    callback(volume);
  }).catch(function(error) {
    console.log('Failed to get initial volume, using default:', constants.VOLUME_CONFIG.DEFAULT);
    callback(constants.VOLUME_CONFIG.DEFAULT);
  });
};

/**
 * Sends the accumulated volume change to API
 */
NowPlayingManager.prototype.sendAccumulatedVolumeChange = function() {
  var self = this;
  
  if (nowPlayingState.cachedVolume === null) {
    return;
  }
  
  console.log('Sending accumulated volume change to:', nowPlayingState.cachedVolume);
  
  // Make the API call
  this.makeApiCall('/me/player/volume?volume_percent=' + nowPlayingState.cachedVolume, 'PUT');
  
  // Clear the timer
  nowPlayingState.volumeChangeTimer = null;
};

// ============================================================================
// Spotify API Communication
// ============================================================================

/**
 * Makes authenticated API calls to Spotify
 * Tracks volume API calls to prevent cache conflicts
 */
NowPlayingManager.prototype.makeApiCall = function(path, method) {
  if (!nowPlayingState.accessToken) {
    this.sendError('No access token available');
    return;
  }
  
  var self = this;
  
  axios({
    url: constants.SPOTIFY_CONFIG.API_BASE_URL + path,
    method: method,
    headers: {
      'Authorization': 'Bearer ' + nowPlayingState.accessToken,
      'Content-Type': 'application/json'
    }
  }).then(function(response) {
    // API call successful - no need for immediate polling
    // The regular polling cycle will pick up the changes
  }).catch(function(error) {
    if (error.response && error.response.status === constants.HTTP_STATUS.UNAUTHORIZED) {
      self.sendError('Authentication expired');
    } else if (error.response && error.response.status === constants.HTTP_STATUS.FORBIDDEN) {
      self.sendError('Action not allowed');
    } else {
      self.sendError('API call failed');
    }
  });
};

// ============================================================================
// Module Export
// ============================================================================

// Export singleton instance
var nowPlayingManager = new NowPlayingManager();
module.exports = nowPlayingManager;
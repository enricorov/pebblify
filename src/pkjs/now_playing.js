// Now Playing Module for Pebblify
// Handles polling for track information and processing actions from C

var axios = require('axios');
var constants = require('./constants');
var messageKeys = require('message_keys');

// Now playing state
var nowPlayingState = {
  isPolling: false,
  pollingTimer: null,
  lastTrackData: null,
  accessToken: null,
  
  // Volume accumulation state
  pendingVolumeChange: null,
  volumeChangeTimer: null,
  isChangingVolume: false, // Flag to ignore API volume updates during changes
  pendingVolumeApiCalls: 0, // Track number of pending volume API calls
  
  // Cached track data for comparison
  cachedData: {
    trackName: null,
    artistName: null,
    isPlaying: false,
    volumePercent: 50,
    canSkipPrev: true,
    canSkipNext: true
  }
};

function NowPlayingManager() {
  this.setupAppMessageHandlers();
}

NowPlayingManager.prototype.setupAppMessageHandlers = function() {
  var self = this;
  
  Pebble.addEventListener('appmessage', function(e) {
    var message = e.payload;
    
    // Handle action messages from C (using single ACTION key with string values)
    if (message['ACTION'] !== undefined) {
      var action = message['ACTION'];
      console.log('NowPlaying: Received action:', action);
      
      switch (action) {
        case 'skip_next':
          console.log('NowPlaying: Handling skip next');
          self.handleSkipNext();
          break;
        case 'skip_prev':
          console.log('NowPlaying: Handling skip prev');
          self.handleSkipPrev();
          break;
        case 'play_pause':
          console.log('NowPlaying: Handling play/pause');
          self.handlePlayPause();
          break;
        case 'volume_up':
          console.log('NowPlaying: Handling volume up');
          self.handleVolumeUp();
          break;
        case 'volume_down':
          console.log('NowPlaying: Handling volume down');
          self.handleVolumeDown();
          break;
        default:
          console.log('NowPlaying: Unknown action:', action);
      }
    }
    else if (message['START_POLLING'] !== undefined) {
      self.startPolling();
    }
    else if (message['STOP_POLLING'] !== undefined) {
      self.stopPolling();
    }
  });
};

NowPlayingManager.prototype.setAccessToken = function(token) {
  nowPlayingState.accessToken = token;
};

NowPlayingManager.prototype.startPolling = function() {
  if (nowPlayingState.isPolling) {
    return; // Already polling
  }
  
  if (!nowPlayingState.accessToken) {
    console.error('Cannot start polling: no access token');
    return;
  }
  
  nowPlayingState.isPolling = true;
  console.log('Starting now playing polling...');
  
  // Start polling immediately
  this.pollNowPlaying();
  
  // Set up recurring polling
  nowPlayingState.pollingTimer = setInterval(function() {
    this.pollNowPlaying();
  }.bind(this), constants.TIMER_INTERVALS.POLLING_INTERVAL);
};

NowPlayingManager.prototype.stopPolling = function() {
  if (!nowPlayingState.isPolling) {
    return; // Not polling
  }
  
  nowPlayingState.isPolling = false;
  console.log('Stopping now playing polling...');
  
  if (nowPlayingState.pollingTimer) {
    clearInterval(nowPlayingState.pollingTimer);
    nowPlayingState.pollingTimer = null;
  }
};

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
    console.error('Now playing polling failed:', error);
    
    if (error.response && error.response.status === constants.HTTP_STATUS.UNAUTHORIZED) {
      // Token expired, send error to C
      self.sendError('Authentication expired');
    } else if (error.response && error.response.status === constants.HTTP_STATUS.NOT_FOUND) {
      // No active device or track
      self.handleNoActiveSession();
    } else {
      self.sendError('Connection error');
    }
  });
};

NowPlayingManager.prototype.handleNowPlayingResponse = function(data) {
  var newData = this.parseNowPlayingData(data);
  
  // Compare with cached data to detect changes
  if (this.hasDataChanged(newData)) {
    console.log('Track data changed, sending to C');
    
    // Update cache
    nowPlayingState.cachedData = newData;
    
    // Send to C
    this.sendNowPlayingData(newData);
  }
};

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
    
    if (data.device && data.device.volume_percent !== undefined) {
      // Only update volume from API if we're not currently changing volume AND no volume API calls are pending
      if (!nowPlayingState.isChangingVolume && nowPlayingState.pendingVolumeApiCalls === 0) {
        trackData.volumePercent = data.device.volume_percent;
      } else {
        console.log('Ignoring API volume update: isChanging=' + nowPlayingState.isChangingVolume + 
                   ', pendingApiCalls=' + nowPlayingState.pendingVolumeApiCalls + 
                   ', API=' + data.device.volume_percent + ', cached=' + trackData.volumePercent);
      }
    }
    
    if (data.actions) {
      trackData.canSkipPrev = data.actions.disallows && !data.actions.disallows.skipping_prev;
      trackData.canSkipNext = data.actions.disallows && !data.actions.disallows.skipping_next;
    }
  }
  
  return trackData;
};

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
    console.log('Now playing data sent to C');
  }, function(error) {
    console.error('Failed to send now playing data:', error);
  });
};

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

NowPlayingManager.prototype.sendError = function(errorMessage) {
  var message = {
    [messageKeys.API_ERROR]: 1,
    [messageKeys.ERROR_MESSAGE]: errorMessage
  };
  
  Pebble.sendAppMessage(message, function() {
    console.log('Error sent to C:', errorMessage);
  }, function(error) {
    console.error('Failed to send error message:', error);
  });
};

// Action handlers
NowPlayingManager.prototype.handleSkipNext = function() {
  console.log('Handling skip next');
  this.makeApiCall('/me/player/next', 'POST');
};

NowPlayingManager.prototype.handleSkipPrev = function() {
  console.log('Handling skip prev');
  this.makeApiCall('/me/player/previous', 'POST');
};

NowPlayingManager.prototype.handlePlayPause = function() {
  console.log('Handling play/pause');
  var action = nowPlayingState.cachedData.isPlaying ? 'pause' : 'play';
  this.makeApiCall('/me/player/' + action, 'PUT');
};

NowPlayingManager.prototype.handleVolumeUp = function() {
  console.log('Handling volume up');
  this.handleVolumeChange(1); // +1 step
};

NowPlayingManager.prototype.handleVolumeDown = function() {
  console.log('Handling volume down');
  this.handleVolumeChange(-1); // -1 step
};

NowPlayingManager.prototype.handleVolumeChange = function(direction) {
  var self = this;
  
  console.log('Volume change request: direction=' + direction + ', current=' + nowPlayingState.cachedData.volumePercent + ', pendingApiCalls=' + nowPlayingState.pendingVolumeApiCalls);
  
  // If there's already a pending volume change, accumulate it
  if (nowPlayingState.pendingVolumeChange) {
    console.log('Accumulating volume change: current steps=' + nowPlayingState.pendingVolumeChange.steps + ', adding=' + direction);
    
    // Cancel the existing timer
    if (nowPlayingState.volumeChangeTimer) {
      clearTimeout(nowPlayingState.volumeChangeTimer);
    }
    
    // Add to the step count
    nowPlayingState.pendingVolumeChange.steps += direction;
    
    // Clamp the total steps to valid range
    var originalVolume = nowPlayingState.pendingVolumeChange.originalVolume;
    var newVolume = originalVolume + (nowPlayingState.pendingVolumeChange.steps * constants.VOLUME_CONFIG.STEP_SIZE);
    
    if (newVolume < constants.VOLUME_CONFIG.MIN) {
      newVolume = constants.VOLUME_CONFIG.MIN;
      nowPlayingState.pendingVolumeChange.steps = Math.floor((newVolume - originalVolume) / constants.VOLUME_CONFIG.STEP_SIZE);
    } else if (newVolume > constants.VOLUME_CONFIG.MAX) {
      newVolume = constants.VOLUME_CONFIG.MAX;
      nowPlayingState.pendingVolumeChange.steps = Math.floor((newVolume - originalVolume) / constants.VOLUME_CONFIG.STEP_SIZE);
    }
    
    console.log('Accumulated volume change: total steps=' + nowPlayingState.pendingVolumeChange.steps + ', final volume=' + newVolume);
    
    // Set timer to send accumulated changes after 500ms
    nowPlayingState.volumeChangeTimer = setTimeout(function() {
      self.executeAccumulatedVolumeChange();
    }, 500); // 500ms delay for accumulated changes
    
  } else {
    // First button press - send immediately and start accumulation
    var currentVolume = nowPlayingState.cachedData.volumePercent;
    var targetVolume = Math.max(constants.VOLUME_CONFIG.MIN, 
                                Math.min(constants.VOLUME_CONFIG.MAX, 
                                        currentVolume + (direction * constants.VOLUME_CONFIG.STEP_SIZE)));
    
    console.log('First volume change: immediate API call from ' + currentVolume + ' to ' + targetVolume);
    
    // Send immediate API call and track it
    nowPlayingState.pendingVolumeApiCalls++;
    this.makeApiCall('/me/player/volume?volume_percent=' + targetVolume, 'PUT');
    
    // Start accumulation for rapid presses
    nowPlayingState.pendingVolumeChange = {
      originalVolume: currentVolume,
      steps: direction
    };
    nowPlayingState.isChangingVolume = true;
    
    // Set timer to send accumulated changes after 500ms
    nowPlayingState.volumeChangeTimer = setTimeout(function() {
      self.executeAccumulatedVolumeChange();
    }, 500); // 500ms delay
  }
};

NowPlayingManager.prototype.executeAccumulatedVolumeChange = function() {
  var self = this;
  
  if (!nowPlayingState.pendingVolumeChange) {
    console.log('No accumulated volume change to execute');
    nowPlayingState.isChangingVolume = false;
    return;
  }
  
  var targetVolume = nowPlayingState.pendingVolumeChange.originalVolume + 
                    (nowPlayingState.pendingVolumeChange.steps * constants.VOLUME_CONFIG.STEP_SIZE);
  
  // Clamp to valid range
  if (targetVolume < constants.VOLUME_CONFIG.MIN) targetVolume = constants.VOLUME_CONFIG.MIN;
  if (targetVolume > constants.VOLUME_CONFIG.MAX) targetVolume = constants.VOLUME_CONFIG.MAX;
  
  console.log('Executing accumulated volume change: steps=' + nowPlayingState.pendingVolumeChange.steps + 
              ', from=' + nowPlayingState.pendingVolumeChange.originalVolume + 
              ', to=' + targetVolume);
  
  // Make the API call for accumulated changes and track it
  nowPlayingState.pendingVolumeApiCalls++;
  this.makeApiCall('/me/player/volume?volume_percent=' + targetVolume, 'PUT');
  
  // Clear the pending change and reset flag
  nowPlayingState.pendingVolumeChange = null;
  nowPlayingState.volumeChangeTimer = null;
  nowPlayingState.isChangingVolume = false;
};

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
    console.log('API call successful:', path);
    
    // If this was a volume API call, decrement the counter
    if (path.includes('/me/player/volume')) {
      nowPlayingState.pendingVolumeApiCalls--;
      console.log('Volume API response received, pending calls remaining:', nowPlayingState.pendingVolumeApiCalls);
      
      // If this was the last pending volume API call, update cached volume
      if (nowPlayingState.pendingVolumeApiCalls === 0) {
        console.log('All volume API calls complete, will update cached volume on next poll');
      }
    }
    
    // Trigger immediate polling to get updated state
    setTimeout(function() {
      self.pollNowPlaying();
    }, 500);
  }).catch(function(error) {
    console.error('API call failed:', path, error);
    
    if (error.response && error.response.status === constants.HTTP_STATUS.UNAUTHORIZED) {
      self.sendError('Authentication expired');
    } else if (error.response && error.response.status === constants.HTTP_STATUS.FORBIDDEN) {
      self.sendError('Action not allowed');
    } else {
      self.sendError('API call failed');
    }
  });
};

// Export singleton instance
var nowPlayingManager = new NowPlayingManager();
module.exports = nowPlayingManager;

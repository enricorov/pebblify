var Settings = require('pebblejs/settings');
var Vibe = require('pebblejs/ui/vibe');
const { PebblifyCard, PebblifyMenu } = require('./windows');

const {
  PEBBLE_ACTIONS,
  API_PATHS,
  SPOTIFY_GREEN,
  COLOR_BLACK,
  COLOR_WHITE,
  NOW_PLAYING_REFRESH_SECONDS,
  APP_MESSAGES,
} = require('./constants');

const SpotifyAuth = require('./spotify');

const spotifyAuth = new SpotifyAuth();

class NowPlayingCard extends PebblifyCard {
  constructor() {
    super();
    this._isActiveSession = false;
    // this._volumeActionsMode = true;
    this._toolsActionsMode = false;

    this.subtitleColor(COLOR_BLACK);
    this.subtitle(APP_MESSAGES.NO_ACTIVE_SESSION);
  }

  init() {
    this.on('show', () => {
      this.autoRefresh = true;
    });

    this.on('hide', () => {
      this.autoRefresh = false;
    });

    this.on('click', (event) => {
      if (this.isActiveSession) {
        let playbackAction;
        let httpMethod;
        let makeCall = true;

        if (this.toolsActionsMode) {
        } else {
          httpMethod = 'put';
          switch (event.button) {
            case PEBBLE_ACTIONS.UP:
              playbackAction = 'volume';
              break;
            case PEBBLE_ACTIONS.SELECT:
              playbackAction = this.currentSession.is_playing
                ? 'pause'
                : 'play';
              break;
            case PEBBLE_ACTIONS.DOWN:
              playbackAction = 'volume';
              break;
            default:
              break;
          }

        if (playbackAction) {
          let volumePercent = 0;
          if (playbackAction == 'volume') {
            volumePercent = this.currentSession.device.volume_percent;
            volumePercent =
              event.button == PEBBLE_ACTIONS.UP
                ? volumePercent + 10
                : volumePercent - 10;

            // volumePercent must be between 0 and 100
            if (volumePercent < 0 || volumePercent > 100) {
              // round to 0 or 100
              volumePercent = Math.round(volumePercent / 100) * 100;
            }
            // do not make a call if the volume stays the same (0, 100)
            makeCall =
              volumePercent != this.currentSession.device.volume_percent;
          }

          if (makeCall) {
            // preemptively update the session volumePercent so repeated calls use the correct value
            this.currentSession.device.volume_percent = volumePercent;
            spotifyAuth.makeCall(
              `${API_PATHS.PLAYER}/${playbackAction}${
                playbackAction == 'volume'
                  ? `?volume_percent=${volumePercent}`
                  : ''
              }`,
              (data) => {
                this.refresh();
              },
              (response) => {
                if (response.data) {
                  if (
                    response.data.error.status == 404 &&
                    response.data.error.reason == 'NO_ACTIVE_DEVICE'
                  ) {
                    // refresh & session is not active
                    this.isActiveSession = false;
                  } else {
                    this.isActiveSession = true;
                    this.refresh();
                  }
                } else {
                  this.isActiveSession = true;
                  this.refresh();
                }
              },
              {},
              httpMethod
            );
          }
        }
      }
    }
    });

    this.on('longClick', (event) => {
      if (this.isActiveSession) {
        let playbackAction;
        let httpMethod;
        // let makeCall = true;

        httpMethod = 'POST'

        switch (event.button) {
          case PEBBLE_ACTIONS.UP:
            if (this.currentSession.actions.disallows.skipping_prev) {
              Vibe.vibrate('double');
              this.action({
                up: 'IMAGE_ICON_DISMISS',
              });
              
            } else {
              playbackAction = 'previous'
              Vibe.vibrate('short');

              this.action({
                up: 'IMAGE_MUSIC_ICON_BACKWARD',
              });
            }
            break;
          case PEBBLE_ACTIONS.SELECT:
            if (!this.toolsActionsMode) {
              this.toolsActionsMode = true;
              Vibe.vibrate('short');
            }
            break;
          case PEBBLE_ACTIONS.DOWN:
            if (this.currentSession.actions.disallows.skipping_next) {
              Vibe.vibrate('double');
              this.action({
                down: 'IMAGE_ICON_DISMISS',
              });

            } else {
              playbackAction = 'next';
              Vibe.vibrate('short');

              this.action({
                down: 'IMAGE_MUSIC_ICON_FORWARD',
              });
            }
            break;
          default:
            break;

        }

        if (playbackAction) {

          let volumePercent = 0;
          volumePercent = this.currentSession.device.volume_percent;

          console.log(`Sending command ${playbackAction} with method ${httpMethod}`);

          spotifyAuth.makeCall(
            `${API_PATHS.PLAYER}/${playbackAction}${playbackAction == 'volume'
              ? `?volume_percent=${volumePercent}`
              : ''
            }`,
            (data) => {
              this.refresh();
            },
            (response) => {
              if (response.data) {
      if (
                  response.data.error.status == 404 &&
                  response.data.error.reason == 'NO_ACTIVE_DEVICE'
      ) {
                  // refresh & session is not active
                  console.log(`Request error status: ${response.data.error.status} | reason: ${response.data.error.reason}`);
                  this.isActiveSession = false;
                } else {
                  this.isActiveSession = true;
                  this.refresh();
                }
              } else {
                console.log(`Command ${playbackAction} sent OK`)
                this.isActiveSession = true;
                this.refresh();
              }
            },
            {},
            httpMethod
          );
        }
      }
    });
  }

  get currentSession() {
    return this._currentSession;
  }

  /**
   * @param {object} sessionData
   */
  set currentSession(sessionData) {
    this._currentSession = sessionData;
  }

  // get volumeActionsMode() {
  //   return this._volumeActionsMode;
  // }

  // /**
  //  * @param {boolean} isVolumeActionsMode
  //  */
  // set volumeActionsMode(isVolumeActionsMode) {
  //   this._volumeActionsMode = isVolumeActionsMode;

  //   if (isVolumeActionsMode) {
  //     this.action({
  //       up: 'IMAGE_MUSIC_ICON_VOLUME_UP',
  // select: this.currentSession.is_playing
  //   ? 'IMAGE_MUSIC_ICON_PAUSE'
  //   : 'IMAGE_MUSIC_ICON_PLAY',
  //       down: 'IMAGE_MUSIC_ICON_VOLUME_DOWN',
  //     });

  //     setTimeout(() => {
  //       this._volumeActionsMode = true;
  //       this.setDefaultActionsMode();
  //     }, 2000);
  //   }
  // }
  /**
   * Sets the card actions to Vol UP, Pause, Vol DOWN
   */
  setDefaultActionsMode() {
      this.action({
        up: 'IMAGE_MUSIC_ICON_VOLUME_UP',
        select: this.currentSession.is_playing
          ? 'IMAGE_MUSIC_ICON_PAUSE'
          : 'IMAGE_MUSIC_ICON_PLAY',
        down: 'IMAGE_MUSIC_ICON_VOLUME_DOWN',
      });

  }

  get toolsActionsMode() {
    return this._toolsActionsMode;
  }

  /**
   * @param {boolean} isToolsActionsMode
   */
  set toolsActionsMode(isToolsActionsMode) {
    this._toolsActionsMode = isToolsActionsMode;

    if (isToolsActionsMode) {
      this.action({
        up: 'IMAGE_MUSIC_ICON_SHUFFLE',
        select: 'IMAGE_MUSIC_ICON_FAVORITE',
        down: 'IMAGE_MUSIC_ICON_ELLIPSIS',
      });

      setTimeout(() => {
        this._toolsActionsMode = false;
        this.setDefaultActionsMode();
      }, 2000);
    }
  }


  get isActiveSession() {
    return this._isActiveSession;
  }

  /**
   * @param {boolean} isActive
   */
  set isActiveSession(isActive) {
    if (isActive != this._isActiveSession) {
      this._isActiveSession = isActive;

      if (!isActive) {
        this.subtitle(APP_MESSAGES.NO_ACTIVE_SESSION);
        this.body('');

        this.subtitleColor(COLOR_BLACK);
        this.backgroundColor(SPOTIFY_GREEN);

        this.action(false);
      } else {
        this.action(true);
      }
    }
  }

  /**
   * @param {boolean} enabled
   */
  set autoRefresh(enabled) {
    if (enabled) {
      if (this._refreshTimer) {
        // a setInterval is already running
        clearInterval(this._refreshTimer);
      }
      this.refresh();
      let timerId = setInterval(() => {
        this.refresh();
      }, NOW_PLAYING_REFRESH_SECONDS * 1000);
      // set a new refreshTimer
      this._refreshTimer = timerId;
    } else {
      clearInterval(this._refreshTimer);
    }
  }

  refresh() {
    spotifyAuth.makeCall(
      API_PATHS.PLAYER,
      (data) => {
        this.isActiveSession = true;
        this.currentSession = data;

        this.subtitleColor(COLOR_WHITE);
        this.subtitle(
          data.item.name.includes(' - ')
            ? data.item.name.split(' - ')[0]
            : data.item.name
        );
        this.body(data.item.artists.map((e) => `🎤 ${e.name}`).join('\n'));

        this.backgroundColor(SPOTIFY_GREEN);

        if (!this.toolsActionsMode) {
          this.setDefaultActionsMode();
        }
      },
      (data) => {
        this.isActiveSession = false;
      }
    );
  }
}

class PebblifyApp {
  constructor() {
    this._nowPlayingCard = new NowPlayingCard();
  }

  get nowPlayingCard() {
    return this._nowPlayingCard;
  }
}

const main = () => {
  let userTokens = Settings.option('userTokens');

  // user has authorized the app
  let tokenHasExpired =
    new Date().getTime() > userTokens.expiration_date - 120 * 1000;

  if (tokenHasExpired) {
    // access token is expired, request a new one using the refresh_token
    let refreshTimerId = Settings.data('refreshTimerId');
    if (refreshTimerId) {
      // clear the last refreshToken setInterval
      clearInterval(refreshTimerId);
    }

    spotifyAuth.refreshToken();

    // refresh the access_token two minutes before it expires
    let newTimerId = setInterval(() => {
      spotifyAuth.refreshToken();
    }, (userTokens.expires_in - 120) * 1000);
    Settings.data('refreshTimerId', newTimerId);
  }

  var app = new PebblifyApp();
  var mainMenu = new PebblifyMenu();

  mainMenu.sections([
    {
      title: 'Home',
      items: [
        {
          title: 'Now playing',
        },
        {
          title: 'Jump back in',
        },
        {
          title: 'Made for you',
        },
      ],
    },
    {
      title: 'Library',
      items: [
        {
          title: 'Playlists',
        },
        {
          title: 'Albums',
        },
        {
          title: 'Artists',
        },
      ],
    },
    {
      title: 'Devices',
      items: [
        {
          title: 'Play on device',
        },
      ],
    },
  ]);

  mainMenu.show();
  app.nowPlayingCard.init();

  mainMenu.on('select', (e) => {
    switch (e.sectionIndex) {
      case 0:
        switch (e.itemIndex) {
          case 0:
            app.nowPlayingCard.show();
            break;
          default:
            break;
        }
        break;
      case 1:
        switch (e.itemIndex) {
          case 0:
            let items = [];
            let next;
            let playlistsMenu = new PebblifyMenu();
            playlistsMenu.status(false);

            playlistsMenu.on('select', (e) => {
              if (e.item.idKey == 'loadmore') {
                getPlaylists(false, next);
              } else {
                // implement songs list here
              }
            });
            playlistsMenu.show();

            let playlistSuccess = (data) => {
              next = data.next;
              data.items.map((x) => {
                x.idKey = x.id;
                x.title = x.name;
                x.subtitle = `${x.tracks.total} tracks`;
              });
              data.items.push({
                idKey: 'loadmore',
                title: 'Load more',
                subtitle: 'Press here!',
                icon: 'IMAGE_ICON_DOWN',
              });
              items.pop();
              items.splice(items.length, 0, ...data.items);
              playlistsMenu.items(0, items);
            };

            let playlistFailure = (data) => {
              console.log(JSON.stringify(data));
            };

            getPlaylists = (firstlaunch, next) => {
              if (firstlaunch || next) {
                spotifyAuth.makeCall(
                  firstlaunch
                    ? API_PATHS.PLAYLISTS
                    : next.split('api.spotify.com/v1').pop(),
                  playlistSuccess,
                  playlistFailure
                );
              } else {
                // there is nothing more to load, remove the last 'Load more' row
                items.pop();
                playlistsMenu.items(0, items);
              }
            };

            getPlaylists(true);
            break;
          default:
            break;
        }
        break;
      case 2:
        break;
      default:
        break;
    }
  });
};

module.exports = main;

# Pebblify - Modular Architecture

## Overview
The Pebblify Spotify app has been refactored from a monolithic `main.c` file into a clean, modular architecture with organized subfolders for each major component.

## File Structure

```
src/c/
├── main.c                    # Entry point and module coordination
├── core/                     # Core app data and state management
│   ├── app_state.h
│   └── app_state.c
├── windows/                  # UI windows and layers
│   ├── main_menu.h/c         # Main menu layer and navigation
│   ├── auth_window.h/c       # Authentication window and Spotify OAuth
│   └── now_playing.h/c       # Now playing window with music controls
└── api/                      # External API integration
    ├── spotify_api.h
    └── spotify_api.c
```

## Module Responsibilities

### `core/` - Core App State
- **`app_state.h/c`** - Global app data structure (`AppData`), authentication data persistence, bitmap caching system, app state initialization/cleanup

### `windows/` - User Interface
- **`main_menu.h/c`** - Main menu layer creation and management, menu item callbacks and navigation, window lifecycle management
- **`auth_window.h/c`** - Authentication window UI, Spotify OAuth flow initiation, authentication success/error handling, window creation/destruction
- **`now_playing.h/c`** - Now playing window UI (track info, clock, action bar), music control handlers (play/pause, volume, skip), volume change state management, error display and recovery, dynamic text layout and sizing

### `api/` - External Integration
- **`spotify_api.h/c`** - Spotify Web API calls, AppMessage communication with JavaScript, API response parsing and error handling, message routing to appropriate modules

## Benefits of Organized Modular Architecture

1. **Maintainability** - Each module has a single responsibility and clear location
2. **Readability** - Logical folder structure makes code navigation intuitive
3. **Scalability** - Easy to add new modules in appropriate folders
4. **Collaboration** - Multiple developers can work on different folders/modules
5. **Debugging** - Issues can be isolated to specific folders and modules
6. **Professional Structure** - Industry-standard organization patterns

## Module Dependencies

```
main.c
├── core/app_state (core data)
├── api/spotify_api (API integration)
├── windows/main_menu (base UI)
├── windows/auth_window (authentication)
└── windows/now_playing (music controls)
```

## Future Expansion

The organized structure makes it easy to add new features:

### `windows/` folder:
- **`playlists.h/c`** - Playlist management window
- **`search.h/c`** - Music search window
- **`settings.h/c`** - App configuration window
- **`devices.h/c`** - Device selection window

### `api/` folder:
- **`playlist_api.h/c`** - Playlist-specific API calls
- **`search_api.h/c`** - Search API integration
- **`device_api.h/c`** - Device management API

### `core/` folder:
- **`config.h/c`** - App configuration management
- **`cache.h/c`** - Data caching system
- **`utils.h/c`** - Shared utility functions

Each new module can be developed independently and integrated through the existing module interfaces, maintaining the clean separation of concerns.

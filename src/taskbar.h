#pragma once

#include <string>
#include <vector>

namespace taskbar {

struct DisplayState {
  std::wstring deviceName;
  std::wstring displayLabel;
  bool isPrimary = false;
  bool hasTaskbar = false;
  bool autoHide = false;
};

// Query all displays with their current auto-hide preference.
// Uses saved preferences if available, otherwise the global state.
std::vector<DisplayState> QueryDisplays();

// Persist a per-monitor auto-hide preference.
void SavePreference(const std::wstring& deviceName, bool autoHide);

// Read/write the Windows global auto-hide toggle via SHAppBarMessage.
bool GetGlobalAutoHide();
void SetGlobalAutoHide(bool autoHide);

// If a previous session crashed while the primary taskbar was hidden,
// detect the leftover state and restore it.  Call once at startup.
void RecoverFromCrash();

// Apply saved preferences.  Turns global auto-hide OFF and builds the
// internal list of taskbar windows that this app will manually hide.
void ApplyPreferences();

// Called on a fast timer (~50 ms).  For every "auto-hide" monitor, checks
// the cursor position and hides or shows the taskbar window accordingly.
void Enforce();

// Show every hidden taskbar and release control (call before exit).
void RestoreAll();

// Nuclear reset: restore all taskbars, turn off global auto-hide,
// delete all saved preferences.  Leaves the system as if the app
// was never installed.
void FactoryReset();

} // namespace taskbar

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
  // Honored only while autoHide is on. Maximized windows then use the whole
  // monitor, and the taskbar draws on top of them when revealed.
  bool fullWorkArea = false;
};

// Saved per-monitor preference when one exists, otherwise the global
// Windows auto-hide state.
std::vector<DisplayState> QueryDisplays();

void SavePreference(const std::wstring& deviceName, bool autoHide);
void SaveFullWorkAreaPreference(const std::wstring& deviceName,
                                bool fullWorkArea);

// The single Windows auto-hide toggle. This app turns it off and hides
// taskbars itself.
bool GetGlobalAutoHide();
void SetGlobalAutoHide(bool autoHide);

// Undo a hidden taskbar or expanded work area left by a previous process.
// Call once at startup, before ApplyPreferences.
void RecoverFromCrash();

// Turn global auto-hide off and hide the taskbars selected by saved preferences.
void ApplyPreferences();

// Show or hide managed taskbars from the cursor position. Called about every 50 ms.
void Enforce();

// Show managed taskbars, restore their work areas, and drop control.
void RestoreAll();

// Restore taskbars, turn off global auto-hide, and delete this app's
// registry tree. Does not remove the startup entry.
void FactoryReset();

} // namespace taskbar

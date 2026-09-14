#include "taskbar.h"

#include "monitor.h"
#include "registry_key.h"

#include <windows.h>
#include <shellapi.h>

#include <optional>
#include <string>
#include <vector>

namespace taskbar {

namespace {

constexpr const wchar_t* kAppKey = L"Software\\PerMonitorTaskbar";
constexpr const wchar_t* kPrefsKey =
    L"Software\\PerMonitorTaskbar\\Monitors";
constexpr int kHotZonePixels = 48;

// -- taskbar window discovery ------------------------------------------------

struct TaskbarWindow {
  HWND hwnd;
  HMONITOR monitor;
};

BOOL CALLBACK CollectTaskbars(HWND hwnd, LPARAM lParam) {
  auto* out = reinterpret_cast<std::vector<TaskbarWindow>*>(lParam);

  if (!IsWindowVisible(hwnd))
    return TRUE;

  wchar_t cls[64];
  if (GetClassNameW(hwnd, cls, 64) == 0)
    return TRUE;

  bool isTaskbar = _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
                   _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
  if (!isTaskbar)
    return TRUE;

  if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD)
    return TRUE;

  TaskbarWindow tw;
  tw.hwnd = hwnd;
  tw.monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  out->push_back(tw);
  return TRUE;
}

std::vector<TaskbarWindow> FindTaskbars() {
  std::vector<TaskbarWindow> result;
  EnumWindows(CollectTaskbars, reinterpret_cast<LPARAM>(&result));
  return result;
}

// -- preference helpers ------------------------------------------------------

std::optional<bool> LoadPreference(const std::wstring& deviceName) {
  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kPrefsKey))
    return std::nullopt;
  auto val = key.ReadDword(deviceName.c_str());
  if (val.has_value())
    return *val != 0;
  return std::nullopt;
}

// -- display label -----------------------------------------------------------

std::wstring BuildDisplayLabel(const monitor::Info& mon) {
  int w = mon.bounds.right - mon.bounds.left;
  int h = mon.bounds.bottom - mon.bounds.top;

  std::wstring label = mon.friendlyName;
  if (mon.isPrimary)
    label += L" (Primary, ";
  else
    label += L" (";
  label += std::to_wstring(w) + L"\u00D7" + std::to_wstring(h) + L")";
  return label;
}

// -- managed taskbar state ---------------------------------------------------

struct ManagedTaskbar {
  HWND hwnd;
  RECT monitorBounds;
  bool hidden;
  LONG_PTR originalExStyle;
};

std::vector<ManagedTaskbar> g_managed;
bool g_active = false;

bool IsCursorInHotZone(const POINT& pt, const ManagedTaskbar& mt) {
  if (pt.x < mt.monitorBounds.left || pt.x >= mt.monitorBounds.right)
    return false;
  // Stay inside this monitor. A stacked display below shares this bottom
  // edge, so y >= bottom - hotZone without an upper bound would treat the
  // entire monitor below as a reveal zone.
  return pt.y >= mt.monitorBounds.bottom - kHotZonePixels &&
         pt.y < mt.monitorBounds.bottom;
}

void RestoreWindowVisuals(HWND hwnd, std::optional<LONG_PTR> originalExStyle) {
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  // Make the window opaque while still layered, THEN change styles.
  // Removing WS_EX_LAYERED while alpha is 0 can leave the window blank.
  if (ex & WS_EX_LAYERED)
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

  LONG_PTR restored;
  if (originalExStyle.has_value()) {
    restored = *originalExStyle;
  } else {
    // Do not strip WS_EX_LAYERED without the saved original. Explorer uses
    // it for acrylic; this app's hide adds WS_EX_TRANSPARENT on top.
    restored = ex & ~WS_EX_TRANSPARENT;
  }
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, restored);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  RedrawWindow(hwnd, nullptr, nullptr,
               RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void RestoreIfClickThrough(HWND hwnd) {
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (ex & WS_EX_TRANSPARENT)
    RestoreWindowVisuals(hwnd, std::nullopt);
}

void RestoreSecondaryIfSlidOff(HWND hwnd) {
  wchar_t cls[64];
  if (GetClassNameW(hwnd, cls, 64) == 0)
    return;
  if (_wcsicmp(cls, L"Shell_SecondaryTrayWnd") != 0)
    return;

  RECT rc;
  if (!GetWindowRect(hwnd, &rc))
    return;

  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(mon, &mi))
    return;

  const int height = rc.bottom - rc.top;
  if (height <= 0)
    return;

  // 1.0 hid secondaries by sliding them so only ~2 px stayed on-screen.
  // Only snap back a bar that is hanging off the bottom edge.
  if (rc.top < mi.rcMonitor.bottom - 8)
    return;

  SetWindowPos(hwnd, HWND_TOPMOST, mi.rcMonitor.left,
               mi.rcMonitor.bottom - height, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE);
}

void UndoLeftoverTaskbarState() {
  for (const auto& tw : FindTaskbars()) {
    RestoreIfClickThrough(tw.hwnd);
    RestoreSecondaryIfSlidOff(tw.hwnd);
  }
}

bool IsEffectivelyHidden(HWND hwnd) {
  LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if (!(ex & WS_EX_LAYERED) || !(ex & WS_EX_TRANSPARENT))
    return false;
  BYTE alpha = 255;
  DWORD flags = 0;
  if (!GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags))
    return false;
  return (flags & LWA_ALPHA) != 0 && alpha == 0;
}

// Hide in place: transparency + click-through. Sliding a secondary bar
// below the monitor edge paints it on any display stacked underneath.

void DoHide(ManagedTaskbar& mt) {
  LONG_PTR ex = GetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE,
                    ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
  SetLayeredWindowAttributes(mt.hwnd, 0, 0, LWA_ALPHA);
  mt.hidden = true;
}

void DoShow(ManagedTaskbar& mt) {
  RestoreWindowVisuals(mt.hwnd, mt.originalExStyle);
  mt.hidden = false;
}

void DoRestore(ManagedTaskbar& mt) {
  RestoreWindowVisuals(mt.hwnd, mt.originalExStyle);
  mt.hidden = false;
}

// -- dirty-flag bookkeeping --------------------------------------------------

void MarkManagedDirty(bool isPrimary, LONG_PTR originalExStyle) {
  RegKey key;
  if (key.Create(HKEY_CURRENT_USER, kAppKey)) {
    key.WriteDword(L"PrimaryManaged", 1);
    if (isPrimary)
      key.WriteDword(L"PrimaryOrigExStyle",
                     static_cast<DWORD>(originalExStyle));
  }
}

void ClearPrimaryDirty() {
  RegKey key;
  if (key.Open(HKEY_CURRENT_USER, kAppKey, KEY_WRITE)) {
    key.WriteDword(L"PrimaryManaged", 0);
    key.DeleteValue(L"PrimaryOrigExStyle");
  }
}

} // namespace

// -- public API --------------------------------------------------------------

std::vector<DisplayState> QueryDisplays() {
  auto monitors = monitor::Enumerate();
  auto taskbars = FindTaskbars();
  bool globalState = GetGlobalAutoHide();

  std::vector<DisplayState> result;
  result.reserve(monitors.size());

  for (const auto& mon : monitors) {
    DisplayState ds;
    ds.deviceName = mon.deviceName;
    ds.displayLabel = BuildDisplayLabel(mon);
    ds.isPrimary = mon.isPrimary;

    for (const auto& tw : taskbars) {
      if (tw.monitor == mon.handle) {
        ds.hasTaskbar = true;
        break;
      }
    }

    auto pref = LoadPreference(mon.deviceName);
    ds.autoHide = pref.value_or(globalState);

    result.push_back(std::move(ds));
  }

  return result;
}

void RecoverFromCrash() {
  // Always undo leftover click-through / 1.0 slide, even if the dirty flag
  // was not written. Otherwise ApplyPreferences can capture a broken style
  // as "original" and restore would keep the bar invisible.
  UndoLeftoverTaskbarState();

  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kAppKey))
    return;

  auto flag = key.ReadDword(L"PrimaryManaged");
  if (!flag.has_value() || *flag == 0)
    return;

  auto origStyle = key.ReadDword(L"PrimaryOrigExStyle");
  HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (tray && origStyle.has_value())
    RestoreWindowVisuals(tray, static_cast<LONG_PTR>(*origStyle));

  ClearPrimaryDirty();
}

void SavePreference(const std::wstring& deviceName, bool autoHide) {
  RegKey key;
  if (key.Create(HKEY_CURRENT_USER, kPrefsKey))
    key.WriteDword(deviceName.c_str(), autoHide ? 1u : 0u);
}

bool GetGlobalAutoHide() {
  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  UINT state = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
  return (state & ABS_AUTOHIDE) != 0;
}

void SetGlobalAutoHide(bool autoHide) {
  HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (!tray)
    return;

  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  abd.hWnd = tray;

  UINT current = static_cast<UINT>(SHAppBarMessage(ABM_GETSTATE, &abd));
  LPARAM keepTop = current & ABS_ALWAYSONTOP;

  abd.lParam =
      autoHide ? (ABS_AUTOHIDE | keepTop) : (keepTop ? ABS_ALWAYSONTOP : 0);
  SHAppBarMessage(ABM_SETSTATE, &abd);
}

void ApplyPreferences() {
  RestoreAll();

  auto monitors = monitor::Enumerate();

  bool hasAnyPreference = false;
  bool anyWantAutoHide = false;
  for (const auto& mon : monitors) {
    auto pref = LoadPreference(mon.deviceName);
    if (pref.has_value()) {
      hasAnyPreference = true;
      if (*pref)
        anyWantAutoHide = true;
    }
  }

  if (!hasAnyPreference)
    return;

  SetGlobalAutoHide(false);

  if (!anyWantAutoHide)
    return;

  auto taskbars = FindTaskbars();

  for (const auto& mon : monitors) {
    auto pref = LoadPreference(mon.deviceName);
    if (!pref.has_value() || !*pref)
      continue;

    for (const auto& tw : taskbars) {
      if (tw.monitor == mon.handle) {
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfoW(mon.handle, &mi)) {
          LONG_PTR origEx = GetWindowLongPtrW(tw.hwnd, GWL_EXSTYLE);
          g_managed.push_back({tw.hwnd, mi.rcMonitor, false, origEx});
          g_active = true;
          MarkManagedDirty(mon.isPrimary, origEx);
        }
        break;
      }
    }
  }

  Enforce();
}

void Enforce() {
  if (!g_active || g_managed.empty())
    return;

  POINT cursor;
  GetCursorPos(&cursor);

  for (auto& mt : g_managed) {
    if (!IsWindow(mt.hwnd)) {
      g_managed.clear();
      g_active = false;
      return;
    }

    bool inHotZone = IsCursorInHotZone(cursor, mt);

    if (mt.hidden) {
      if (inHotZone)
        DoShow(mt);
      else if (!IsEffectivelyHidden(mt.hwnd))
        DoHide(mt);
    } else {
      RECT rc;
      GetWindowRect(mt.hwnd, &rc);
      bool cursorInTaskbar = PtInRect(&rc, cursor) != 0;

      if (!inHotZone && !cursorInTaskbar)
        DoHide(mt);
    }
  }
}

void RestoreAll() {
  for (auto& mt : g_managed) {
    if (IsWindow(mt.hwnd))
      DoRestore(mt);
  }
  g_managed.clear();
  g_active = false;
  ClearPrimaryDirty();
}

void FactoryReset() {
  RestoreAll();
  SetGlobalAutoHide(false);

  // Undo leftover click-through / 1.0 slide if RestoreAll missed a bar.
  // Do not strip WS_EX_LAYERED from healthy Explorer taskbars.
  UndoLeftoverTaskbarState();

  // Delete all saved preferences.
  RegDeleteTreeW(HKEY_CURRENT_USER, kAppKey);
}

} // namespace taskbar

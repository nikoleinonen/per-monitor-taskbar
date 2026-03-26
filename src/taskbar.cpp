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
  int fullHeight;
  bool hidden;
  bool isPrimary;
  LONG_PTR originalExStyle;
};

std::vector<ManagedTaskbar> g_managed;
bool g_active = false;

bool IsCursorInHotZone(const POINT& pt, const ManagedTaskbar& mt) {
  if (pt.x < mt.monitorBounds.left || pt.x >= mt.monitorBounds.right)
    return false;
  return pt.y >= mt.monitorBounds.bottom - kHotZonePixels;
}

// Primary taskbar: the shell protects its position, so we hide it by making
// it fully transparent and click-through.
// Secondary taskbars: the shell doesn't fight, so we simply slide them
// off-screen (2 px visible at the edge, matching native auto-hide).

void DoHide(ManagedTaskbar& mt) {
  if (mt.isPrimary) {
    LONG_PTR ex = GetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE,
                      ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(mt.hwnd, 0, 0, LWA_ALPHA);
  } else {
    int newTop = mt.monitorBounds.bottom - 2;
    SetWindowPos(mt.hwnd, nullptr, mt.monitorBounds.left, newTop, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  mt.hidden = true;
}

void DoShow(ManagedTaskbar& mt) {
  if (mt.isPrimary) {
    LONG_PTR ex = GetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE);
    ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE, ex);
    SetLayeredWindowAttributes(mt.hwnd, 0, 255, LWA_ALPHA);
  } else {
    int newTop = mt.monitorBounds.bottom - mt.fullHeight;
    SetWindowPos(mt.hwnd, HWND_TOPMOST, mt.monitorBounds.left, newTop, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
  }
  mt.hidden = false;
}

void DoRestore(ManagedTaskbar& mt) {
  if (mt.isPrimary) {
    SetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE, mt.originalExStyle);
    SetWindowPos(mt.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  } else if (mt.hidden) {
    DoShow(mt);
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
        RECT rc;
        if (GetWindowRect(tw.hwnd, &rc)) {
          MONITORINFO mi = {sizeof(mi)};
          if (GetMonitorInfoW(mon.handle, &mi)) {
            LONG_PTR origEx = GetWindowLongPtrW(tw.hwnd, GWL_EXSTYLE);
            g_managed.push_back({tw.hwnd, mi.rcMonitor, rc.bottom - rc.top,
                                 false, mon.isPrimary, origEx});
            g_active = true;
          }
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

      // Secondary: re-hide if the shell snapped the taskbar back.
      if (!mt.isPrimary && !inHotZone) {
        RECT rc;
        if (GetWindowRect(mt.hwnd, &rc)) {
          int expectedTop = mt.monitorBounds.bottom - 2;
          if (rc.top < expectedTop - 4)
            DoHide(mt);
        }
      }
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
}

} // namespace taskbar

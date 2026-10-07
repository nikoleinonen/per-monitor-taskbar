#include "taskbar.h"

#include "monitor.h"
#include "registry_key.h"

#include <shellapi.h>
#include <windows.h>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace taskbar {

namespace {

constexpr const wchar_t *kAppKey = L"Software\\PerMonitorTaskbar";
constexpr const wchar_t *kPrefsKey = L"Software\\PerMonitorTaskbar\\Monitors";
constexpr const wchar_t *kFullWorkAreaKey =
    L"Software\\PerMonitorTaskbar\\FullWorkArea";
constexpr const wchar_t *kWorkAreaKey =
    L"Software\\PerMonitorTaskbar\\WorkArea";
constexpr int kHotZonePixels = 48;

struct TaskbarWindow {
  HWND hwnd;
  HMONITOR monitor;
};

BOOL CALLBACK CollectTaskbars(HWND hwnd, LPARAM lParam) {
  auto *out = reinterpret_cast<std::vector<TaskbarWindow> *>(lParam);

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

std::optional<bool> LoadPreference(const std::wstring &deviceName) {
  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kPrefsKey))
    return std::nullopt;
  auto val = key.ReadDword(deviceName.c_str());
  if (val.has_value())
    return *val != 0;
  return std::nullopt;
}

std::optional<bool> LoadFullWorkArea(const std::wstring &deviceName) {
  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kFullWorkAreaKey))
    return std::nullopt;
  auto val = key.ReadDword(deviceName.c_str());
  if (val.has_value())
    return *val != 0;
  return std::nullopt;
}

std::wstring BuildDisplayLabel(const monitor::Info &mon) {
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

struct ManagedTaskbar {
  HWND hwnd;
  HMONITOR monitor;
  std::wstring deviceName;
  RECT monitorBounds;
  RECT reservedWorkArea;
  bool hidden;
  bool fullWorkArea;
  LONG_PTR originalExStyle;
  // GetTickCount64 deadline. A failed work-area change must not be retried
  // on every timer tick.
  ULONGLONG workAreaRetryAfter = 0;
};

std::vector<ManagedTaskbar> g_managed;
bool g_active = false;

bool IsCursorInHotZone(const POINT &pt, const ManagedTaskbar &mt) {
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
               mi.rcMonitor.bottom - height, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void UndoLeftoverTaskbarState() {
  for (const auto &tw : FindTaskbars()) {
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

bool SameRect(const RECT &a, const RECT &b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

bool RectInside(const RECT &inner, const RECT &outer) {
  return inner.left >= outer.left && inner.top >= outer.top &&
         inner.right <= outer.right && inner.bottom <= outer.bottom &&
         inner.right > inner.left && inner.bottom > inner.top;
}

bool IsShellOrDesktop(HWND hwnd) {
  wchar_t cls[64];
  if (GetClassNameW(hwnd, cls, 64) == 0)
    return true;
  return _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
         _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0 ||
         _wcsicmp(cls, L"Progman") == 0 || _wcsicmp(cls, L"WorkerW") == 0 ||
         _wcsicmp(cls, L"PerMonitorTaskbarHost") == 0 ||
         _wcsicmp(cls, L"PerMonitorTaskbarSettings") == 0;
}

struct ZoomedNotify {
  HMONITOR monitor;
};

BOOL CALLBACK NotifyZoomedProc(HWND hwnd, LPARAM lp) {
  if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || !IsZoomed(hwnd))
    return TRUE;
  if (IsShellOrDesktop(hwnd))
    return TRUE;
  const auto *ctx = reinterpret_cast<const ZoomedNotify *>(lp);
  if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != ctx->monitor)
    return TRUE;
  // Posted so a hung window cannot stall the timer.
  SendNotifyMessageW(hwnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
  return TRUE;
}

bool CurrentWorkArea(HMONITOR monitor, RECT *out) {
  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(monitor, &mi))
    return false;
  *out = mi.rcWork;
  return true;
}

// Update one monitor's work area without SPIF_SENDCHANGE. Explorer treats
// that broadcast as a cue to put the taskbar reservation back, which made
// the 50 ms timer fight it forever. Already-maximized windows are told
// individually. No SPIF_UPDATEINIFILE, so this does not stick in the profile.
bool CommitWorkArea(HMONITOR monitor, const RECT &rc) {
  if (rc.right <= rc.left || rc.bottom <= rc.top)
    return false;
  RECT current{};
  if (CurrentWorkArea(monitor, &current) && SameRect(current, rc))
    return true;

  RECT copy = rc;
  if (!SystemParametersInfoW(SPI_SETWORKAREA, 0, &copy, 0))
    return false;
  RECT after{};
  if (!CurrentWorkArea(monitor, &after) || !SameRect(after, rc))
    return false;
  ZoomedNotify ctx{monitor};
  EnumWindows(NotifyZoomedProc, reinterpret_cast<LPARAM>(&ctx));
  return true;
}

// Work area Explorer would reserve for a taskbar sitting on one edge.
RECT ReservedFromTaskbar(const RECT &monitor, const RECT &taskbar) {
  RECT inter{};
  if (!IntersectRect(&inter, &monitor, &taskbar))
    return monitor;

  const int width = inter.right - inter.left;
  const int height = inter.bottom - inter.top;
  if (width <= 0 || height <= 0)
    return monitor;

  RECT work = monitor;
  if (height <= width) {
    const int fromTop = inter.top - monitor.top;
    const int fromBottom = monitor.bottom - inter.bottom;
    if (fromTop <= fromBottom)
      work.top = inter.bottom;
    else
      work.bottom = inter.top;
  } else {
    const int fromLeft = inter.left - monitor.left;
    const int fromRight = monitor.right - inter.right;
    if (fromLeft <= fromRight)
      work.left = inter.right;
    else
      work.right = inter.left;
  }
  return work;
}

void SaveWorkAreaDirty(const std::wstring &deviceName, const RECT &rc) {
  RegKey key;
  if (key.Create(HKEY_CURRENT_USER, kWorkAreaKey))
    key.WriteBinary(deviceName.c_str(), reinterpret_cast<const BYTE *>(&rc),
                    sizeof(rc));
}

void ClearWorkAreaDirty(const std::wstring &deviceName) {
  RegKey key;
  if (key.Open(HKEY_CURRENT_USER, kWorkAreaKey, KEY_SET_VALUE))
    key.DeleteValue(deviceName.c_str());
}

struct DirtyWorkArea {
  std::wstring deviceName;
  RECT reserved;
};

std::vector<DirtyWorkArea> ReadDirtyWorkAreas() {
  std::vector<DirtyWorkArea> items;
  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kWorkAreaKey))
    return items;

  DWORD index = 0;
  for (;;) {
    wchar_t name[256];
    DWORD nameLen = 256;
    BYTE data[sizeof(RECT)];
    DWORD dataSize = sizeof(data);
    DWORD type = 0;
    LONG status = RegEnumValueW(key.Get(), index, name, &nameLen, nullptr,
                                &type, data, &dataSize);
    if (status != ERROR_SUCCESS)
      break;
    ++index;
    if (type != REG_BINARY || dataSize != sizeof(RECT))
      continue;
    DirtyWorkArea item;
    item.deviceName = name;
    std::memcpy(&item.reserved, data, sizeof(RECT));
    items.push_back(std::move(item));
  }
  return items;
}

// Put back the reserved work area for monitors this app expanded. A saved
// rectangle is applied only when it still lies inside that same monitor, so
// a stale virtual-screen rect cannot land on a different display.
void RestoreDirtyWorkAreas() {
  auto dirty = ReadDirtyWorkAreas();
  if (dirty.empty())
    return;

  auto monitors = monitor::Enumerate();
  auto taskbars = FindTaskbars();
  bool allRestored = true;

  for (const auto &item : dirty) {
    const monitor::Info *mon = nullptr;
    for (const auto &candidate : monitors) {
      if (candidate.deviceName == item.deviceName) {
        mon = &candidate;
        break;
      }
    }
    if (!mon)
      continue;

    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon->handle, &mi)) {
      allRestored = false;
      continue;
    }

    RECT reserved = item.reserved;
    if (!RectInside(reserved, mi.rcMonitor) ||
        SameRect(reserved, mi.rcMonitor)) {
      reserved = mi.rcMonitor;
      for (const auto &tw : taskbars) {
        if (tw.monitor != mon->handle)
          continue;
        RECT taskbarRect{};
        if (GetWindowRect(tw.hwnd, &taskbarRect))
          reserved = ReservedFromTaskbar(mi.rcMonitor, taskbarRect);
        break;
      }
    }

    if (!SameRect(mi.rcWork, reserved) &&
        !CommitWorkArea(mon->handle, reserved))
      allRestored = false;
  }

  if (allRestored)
    RegDeleteTreeW(HKEY_CURRENT_USER, kWorkAreaKey);
}

void ArmFullWorkArea(ManagedTaskbar &mt) {
  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(mt.monitor, &mi)) {
    mt.fullWorkArea = false;
    return;
  }

  RECT reserved = mi.rcWork;
  if (SameRect(reserved, mi.rcMonitor)) {
    RECT taskbarRect{};
    if (GetWindowRect(mt.hwnd, &taskbarRect))
      reserved = ReservedFromTaskbar(mi.rcMonitor, taskbarRect);
  }

  mt.reservedWorkArea = reserved;
  mt.fullWorkArea = true;
  mt.workAreaRetryAfter = 0;
  SaveWorkAreaDirty(mt.deviceName, reserved);
  CommitWorkArea(mt.monitor, mi.rcMonitor);
}

void RestoreWorkArea(ManagedTaskbar &mt) {
  if (!mt.fullWorkArea)
    return;

  // Keep the saved rectangle if the restore does not stick, so the next
  // startup can try again.
  if (!CommitWorkArea(mt.monitor, mt.reservedWorkArea))
    return;

  ClearWorkAreaDirty(mt.deviceName);
  mt.fullWorkArea = false;
}

// Explorer sometimes writes the reserved work area back. Re-expand here.
// ApplyPreferences would show every taskbar first.
void MaintainWorkArea(ManagedTaskbar &mt) {
  if (!mt.fullWorkArea || !IsWindow(mt.hwnd))
    return;

  MONITORINFO mi{sizeof(mi)};
  if (!GetMonitorInfoW(mt.monitor, &mi))
    return;
  if (SameRect(mi.rcWork, mi.rcMonitor)) {
    mt.workAreaRetryAfter = 0;
    return;
  }

  const ULONGLONG now = GetTickCount64();
  if (now < mt.workAreaRetryAfter)
    return;

  if (RectInside(mi.rcWork, mi.rcMonitor) &&
      !SameRect(mi.rcWork, mt.reservedWorkArea)) {
    mt.reservedWorkArea = mi.rcWork;
    SaveWorkAreaDirty(mt.deviceName, mi.rcWork);
  }

  if (!CommitWorkArea(mt.monitor, mi.rcMonitor))
    mt.workAreaRetryAfter = now + 1000;
}

// Transparency and click-through, in place. Moving a secondary bar past the
// monitor edge paints it onto any display stacked underneath.
void DoHide(ManagedTaskbar &mt) {
  LONG_PTR ex = GetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(mt.hwnd, GWL_EXSTYLE,
                    ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
  SetLayeredWindowAttributes(mt.hwnd, 0, 0, LWA_ALPHA);
  mt.hidden = true;
}

void DoShow(ManagedTaskbar &mt) {
  RestoreWindowVisuals(mt.hwnd, mt.originalExStyle);
  if (mt.fullWorkArea) {
    // The bar stays in the strip the maximized window now occupies, so it
    // has to come up above that window.
    SetWindowPos(mt.hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  mt.hidden = false;
}

void DoRestore(ManagedTaskbar &mt) {
  RestoreWindowVisuals(mt.hwnd, mt.originalExStyle);
  mt.hidden = false;
}

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

std::vector<DisplayState> QueryDisplays() {
  auto monitors = monitor::Enumerate();
  auto taskbars = FindTaskbars();
  bool globalState = GetGlobalAutoHide();

  std::vector<DisplayState> result;
  result.reserve(monitors.size());

  for (const auto &mon : monitors) {
    DisplayState ds;
    ds.deviceName = mon.deviceName;
    ds.displayLabel = BuildDisplayLabel(mon);
    ds.isPrimary = mon.isPrimary;

    for (const auto &tw : taskbars) {
      if (tw.monitor == mon.handle) {
        ds.hasTaskbar = true;
        break;
      }
    }

    auto pref = LoadPreference(mon.deviceName);
    ds.autoHide = pref.value_or(globalState);
    ds.fullWorkArea =
        ds.autoHide && LoadFullWorkArea(mon.deviceName).value_or(false);

    result.push_back(std::move(ds));
  }

  return result;
}

void RecoverFromCrash() {
  // Always undo leftover click-through / 1.0 slide, even if the dirty flag
  // was not written. Otherwise ApplyPreferences can capture a broken style
  // as "original" and restore would keep the bar invisible.
  UndoLeftoverTaskbarState();
  RestoreDirtyWorkAreas();

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

void SavePreference(const std::wstring &deviceName, bool autoHide) {
  RegKey key;
  if (key.Create(HKEY_CURRENT_USER, kPrefsKey))
    key.WriteDword(deviceName.c_str(), autoHide ? 1u : 0u);
}

void SaveFullWorkAreaPreference(const std::wstring &deviceName,
                                bool fullWorkArea) {
  RegKey key;
  if (key.Create(HKEY_CURRENT_USER, kFullWorkAreaKey))
    key.WriteDword(deviceName.c_str(), fullWorkArea ? 1u : 0u);
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
  for (const auto &mon : monitors) {
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

  for (const auto &mon : monitors) {
    auto pref = LoadPreference(mon.deviceName);
    if (!pref.has_value() || !*pref)
      continue;

    for (const auto &tw : taskbars) {
      if (tw.monitor == mon.handle) {
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfoW(mon.handle, &mi)) {
          LONG_PTR origEx = GetWindowLongPtrW(tw.hwnd, GWL_EXSTYLE);
          ManagedTaskbar mt;
          mt.hwnd = tw.hwnd;
          mt.monitor = mon.handle;
          mt.deviceName = mon.deviceName;
          mt.monitorBounds = mi.rcMonitor;
          mt.reservedWorkArea = {};
          mt.hidden = false;
          mt.fullWorkArea = false;
          mt.originalExStyle = origEx;
          mt.workAreaRetryAfter = 0;
          g_managed.push_back(std::move(mt));
          g_active = true;
          MarkManagedDirty(mon.isPrimary, origEx);
          if (LoadFullWorkArea(mon.deviceName).value_or(false))
            ArmFullWorkArea(g_managed.back());
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

  for (auto &mt : g_managed) {
    if (!IsWindow(mt.hwnd)) {
      for (auto &bar : g_managed)
        RestoreWorkArea(bar);
      g_managed.clear();
      g_active = false;
      return;
    }

    MaintainWorkArea(mt);

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
  for (auto &mt : g_managed) {
    if (IsWindow(mt.hwnd))
      DoRestore(mt);
    RestoreWorkArea(mt);
  }
  g_managed.clear();
  g_active = false;
  ClearPrimaryDirty();
}

void FactoryReset() {
  RestoreAll();
  SetGlobalAutoHide(false);

  // RestoreAll only sees bars this session was managing.
  UndoLeftoverTaskbarState();

  RegDeleteTreeW(HKEY_CURRENT_USER, kAppKey);
}

} // namespace taskbar

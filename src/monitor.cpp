#include "monitor.h"

#include <algorithm>

namespace monitor {

namespace {

std::wstring ResolveFriendlyName(const wchar_t* gdiDeviceName) {
  DISPLAY_DEVICEW adapter{};
  adapter.cb = sizeof(adapter);

  for (DWORD ai = 0; EnumDisplayDevicesW(nullptr, ai, &adapter, 0); ++ai) {
    if (!(adapter.StateFlags & DISPLAY_DEVICE_ACTIVE))
      continue;
    if (_wcsicmp(adapter.DeviceName, gdiDeviceName) != 0)
      continue;

    // Found the matching adapter -- get the first active monitor's name
    DISPLAY_DEVICEW mon{};
    mon.cb = sizeof(mon);
    for (DWORD mi = 0; EnumDisplayDevicesW(adapter.DeviceName, mi, &mon, 0);
         ++mi) {
      if (!(mon.StateFlags & DISPLAY_DEVICE_ACTIVE))
        continue;
      std::wstring name = mon.DeviceString;
      if (!name.empty())
        return name;
    }

    // No child monitor entry -- fall back to adapter description
    std::wstring name = adapter.DeviceString;
    if (!name.empty())
      return name;
    break;
  }

  return gdiDeviceName ? gdiDeviceName : L"Unknown";
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT,
                              LPARAM lParam) {
  auto* results = reinterpret_cast<std::vector<Info>*>(lParam);

  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(hMonitor, &mi))
    return TRUE;

  Info info;
  info.handle = hMonitor;
  info.deviceName = mi.szDevice;
  info.friendlyName = ResolveFriendlyName(mi.szDevice);
  info.bounds = mi.rcMonitor;
  info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

  results->push_back(std::move(info));
  return TRUE;
}

} // namespace

std::vector<Info> Enumerate() {
  std::vector<Info> results;
  EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc,
                      reinterpret_cast<LPARAM>(&results));

  // Primary monitor first, then sorted by device name
  std::sort(results.begin(), results.end(),
            [](const Info& a, const Info& b) {
              if (a.isPrimary != b.isPrimary)
                return a.isPrimary;
              return a.deviceName < b.deviceName;
            });

  return results;
}

} // namespace monitor

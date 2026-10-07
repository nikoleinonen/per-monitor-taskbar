#include "monitor.h"

#include <algorithm>
#include <vector>

namespace monitor {

namespace {

// EnumDisplayDevicesW reports the driver description ("Generic PnP Monitor").
// The product name from the panel EDID is available through the display
// configuration API, matched back to the same GDI device (\\.\DISPLAYn).
bool QueryActivePaths(std::vector<DISPLAYCONFIG_PATH_INFO> &paths) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                                    &modeCount) != ERROR_SUCCESS)
      return false;

    if (pathCount == 0) {
      paths.clear();
      return true;
    }

    paths.resize(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    const LONG status = QueryDisplayConfig(
        QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
        modes.empty() ? nullptr : modes.data(), nullptr);
    if (status == ERROR_SUCCESS) {
      paths.resize(pathCount);
      return true;
    }
    if (status != ERROR_INSUFFICIENT_BUFFER)
      return false;
  }
  return false;
}

std::wstring Trim(std::wstring value) {
  const auto isSpace = [](wchar_t c) { return c == L' ' || c == L'\t'; };
  while (!value.empty() && isSpace(value.front()))
    value.erase(value.begin());
  while (!value.empty() && isSpace(value.back()))
    value.pop_back();
  return value;
}

std::wstring
EdidNameForDevice(const std::vector<DISPLAYCONFIG_PATH_INFO> &paths,
                  const wchar_t *gdiDeviceName) {
  if (!gdiDeviceName)
    return {};

  for (const auto &path : paths) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = path.sourceInfo.adapterId;
    source.header.id = path.sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
      continue;
    if (_wcsicmp(source.viewGdiDeviceName, gdiDeviceName) != 0)
      continue;

    DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
    target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    target.header.size = sizeof(target);
    target.header.adapterId = path.targetInfo.adapterId;
    target.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
      continue;

    std::wstring name = Trim(target.monitorFriendlyDeviceName);
    if (!name.empty())
      return name;
  }
  return {};
}

std::wstring ResolveFriendlyName(const wchar_t *gdiDeviceName) {
  DISPLAY_DEVICEW adapter{};
  adapter.cb = sizeof(adapter);

  for (DWORD ai = 0; EnumDisplayDevicesW(nullptr, ai, &adapter, 0); ++ai) {
    if (!(adapter.StateFlags & DISPLAY_DEVICE_ACTIVE))
      continue;
    if (_wcsicmp(adapter.DeviceName, gdiDeviceName) != 0)
      continue;

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

    std::wstring name = adapter.DeviceString;
    if (!name.empty())
      return name;
    break;
  }

  return gdiDeviceName ? gdiDeviceName : L"Unknown";
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
  auto *results = reinterpret_cast<std::vector<Info> *>(lParam);

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

  std::vector<DISPLAYCONFIG_PATH_INFO> paths;
  if (QueryActivePaths(paths)) {
    for (auto &info : results) {
      std::wstring edid = EdidNameForDevice(paths, info.deviceName.c_str());
      if (!edid.empty())
        info.friendlyName = std::move(edid);
    }
  }

  std::sort(results.begin(), results.end(), [](const Info &a, const Info &b) {
    if (a.isPrimary != b.isPrimary)
      return a.isPrimary;
    return a.deviceName < b.deviceName;
  });

  return results;
}

} // namespace monitor

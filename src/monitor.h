#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace monitor {

struct Info {
  HMONITOR handle{};
  std::wstring deviceName;
  std::wstring friendlyName;
  RECT bounds{};
  bool isPrimary = false;
};

std::vector<Info> Enumerate();

} // namespace monitor

#include "application.h"

#include <windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
  HANDLE mutex =
      CreateMutexW(nullptr, FALSE, L"Local\\PerMonitorTaskbar_Singleton");
  if (!mutex)
    return 1;

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND prev = FindWindowW(L"PerMonitorTaskbarHost", nullptr);
    if (prev)
      PostMessageW(prev, WM_APP + 1, 0, 0);
    CloseHandle(mutex);
    return 0;
  }

  Application app;
  if (!app.Init(hInstance)) {
    CloseHandle(mutex);
    return 1;
  }

  int result = app.Run();
  CloseHandle(mutex);
  return result;
}

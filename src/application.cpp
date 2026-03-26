#include "application.h"

#include "registry_key.h"
#include "settings_window.h"
#include "taskbar.h"
#include "resource.h"

#include <shellapi.h>

namespace {

constexpr UINT kTrayCallbackMsg = WM_USER + 1;
constexpr UINT kShowSettingsMsg = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

constexpr UINT_PTR kEnforceTimerId = 1;
constexpr UINT_PTR kDisplayChangeTimerId = 2;

constexpr const wchar_t* kHostClassName = L"PerMonitorTaskbarHost";
constexpr const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValueName = L"PerMonitorTaskbar";

} // namespace

Application* Application::instance_ = nullptr;

Application::Application() { instance_ = this; }

Application::~Application() {
  if (instance_ == this)
    instance_ = nullptr;
}

bool Application::Init(HINSTANCE hInstance) {
  hInstance_ = hInstance;
  taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSW wc{};
  wc.lpfnWndProc = HostWndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = kHostClassName;

  if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;

  // Regular hidden top-level window (NOT HWND_MESSAGE) so that
  // we receive broadcast messages such as TaskbarCreated.
  hostWindow_ =
      CreateWindowExW(0, kHostClassName, L"PerMonitorTaskbar", WS_POPUP, 0, 0,
                      0, 0, nullptr, nullptr, hInstance, nullptr);
  return hostWindow_ != nullptr;
}

int Application::Run() {
  AddTrayIcon();
  taskbar::ApplyPreferences();
  SetTimer(hostWindow_, kEnforceTimerId, 50, nullptr);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  KillTimer(hostWindow_, kEnforceTimerId);
  taskbar::RestoreAll();
  RemoveTrayIcon();
  return static_cast<int>(msg.wParam);
}

void Application::ShowSettings() {
  if (settingsWindow_ && IsWindow(settingsWindow_)) {
    ShowWindow(settingsWindow_, SW_RESTORE);
    SetForegroundWindow(settingsWindow_);
    return;
  }
  settingsWindow_ = settings_window::Create(hInstance_);
}

void Application::AddTrayIcon() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hostWindow_;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = kTrayCallbackMsg;
  nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  lstrcpyW(nid.szTip, L"Per-Monitor Taskbar");
  Shell_NotifyIconW(NIM_ADD, &nid);

  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void Application::RemoveTrayIcon() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hostWindow_;
  nid.uID = kTrayIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
}

void Application::ShowTrayMenu() {
  POINT pt{};
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  AppendMenuW(menu, MF_STRING, 1, L"Settings\u2026");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 2, L"Exit");

  SetForegroundWindow(hostWindow_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                 pt.x, pt.y, 0, hostWindow_, nullptr);
  DestroyMenu(menu);
}

bool Application::GetStartWithWindows() const {
  wchar_t exePath[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return false;

  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kRunKey))
    return false;

  std::wstring regValue = key.ReadString(kRunValueName);
  if (regValue.empty())
    return false;

  if (regValue.size() >= 2 && regValue.front() == L'"' &&
      regValue.back() == L'"')
    regValue = regValue.substr(1, regValue.size() - 2);

  return _wcsicmp(regValue.c_str(), exePath) == 0;
}

void Application::SetStartWithWindows(bool enable) {
  wchar_t exePath[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return;

  RegKey key;
  if (!key.Open(HKEY_CURRENT_USER, kRunKey, KEY_WRITE))
    return;

  if (!enable) {
    key.DeleteValue(kRunValueName);
    return;
  }

  std::wstring cmd(exePath, len);
  if (cmd.find(L' ') != std::wstring::npos)
    cmd = L"\"" + cmd + L"\"";
  key.WriteString(kRunValueName, cmd);
}

LRESULT CALLBACK Application::HostWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam) {
  Application* app = instance_;

  if (app && app->taskbarCreatedMsg_ && msg == app->taskbarCreatedMsg_) {
    app->AddTrayIcon();
    taskbar::ApplyPreferences();
    return 0;
  }

  switch (msg) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  case WM_DISPLAYCHANGE:
  case WM_SETTINGCHANGE:
    SetTimer(hwnd, kDisplayChangeTimerId, 500, nullptr);
    return 0;

  case WM_TIMER:
    if (wParam == kEnforceTimerId) {
      taskbar::Enforce();
      return 0;
    }
    if (wParam == kDisplayChangeTimerId) {
      KillTimer(hwnd, kDisplayChangeTimerId);
      taskbar::ApplyPreferences();
      return 0;
    }
    break;

  case kTrayCallbackMsg:
    if (!app)
      break;
    switch (LOWORD(lParam)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
      app->ShowSettings();
      return 0;
    case WM_CONTEXTMENU:
      app->ShowTrayMenu();
      return 0;
    }
    return 0;

  case WM_COMMAND:
    if (!app)
      break;
    switch (LOWORD(wParam)) {
    case 1:
      app->ShowSettings();
      return 0;
    case 2:
      DestroyWindow(hwnd);
      return 0;
    }
    return 0;

  case kShowSettingsMsg:
    if (app)
      app->ShowSettings();
    return 0;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

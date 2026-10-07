#include "application.h"

#include "registry_key.h"
#include "resource.h"
#include "settings_window.h"
#include "taskbar.h"

#include <commctrl.h>
#include <shellapi.h>

namespace {

constexpr UINT kTrayCallbackMsg = WM_USER + 1;
constexpr UINT kShowSettingsMsg = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

constexpr UINT_PTR kEnforceTimerId = 1;
constexpr UINT_PTR kDisplayChangeTimerId = 2;
constexpr UINT_PTR kTrayIconTimerId = 3;

constexpr const wchar_t *kHostClassName = L"PerMonitorTaskbarHost";
constexpr const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t *kRunValueName = L"PerMonitorTaskbar";

UINT DpiForWindow(HWND hwnd) {
  using Fn = UINT(WINAPI *)(HWND);
  static auto fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn && hwnd) {
    UINT dpi = fn(hwnd);
    if (dpi != 0)
      return dpi;
  }

  HDC hdc = GetDC(nullptr);
  UINT dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
  ReleaseDC(nullptr, hdc);
  return dpi == 0 ? 96 : dpi;
}

int MetricForDpi(int metric, UINT dpi) {
  using Fn = int(WINAPI *)(int, UINT);
  static auto fn = reinterpret_cast<Fn>(GetProcAddress(
      GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
  if (fn) {
    int value = fn(metric, dpi);
    if (value > 0)
      return value;
  }
  return GetSystemMetrics(metric);
}

HICON LoadResourceIcon(HINSTANCE instance, WORD id, int cx) {
  HICON icon = nullptr;
  if (SUCCEEDED(LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(id), cx, cx,
                                      &icon)) &&
      icon)
    return icon;

  return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(id),
                                       IMAGE_ICON, cx, cx, LR_DEFAULTCOLOR));
}

WORD TrayIconResource() {
  // 0 = dark taskbar, 1 = light taskbar. Missing value matches the Windows 11 default.
  RegKey key;
  if (key.Open(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize")) {
    if (auto theme = key.ReadDword(L"SystemUsesLightTheme"))
      return *theme != 0 ? IDI_TRAY_LIGHT : IDI_TRAY_DARK;
  }
  return IDI_TRAY_DARK;
}

struct TrayIconSpec {
  WORD id = IDI_TRAY_DARK;
  int cx = 0;
};

TrayIconSpec CurrentTrayIconSpec() {
  HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
  TrayIconSpec spec;
  spec.id = TrayIconResource();
  spec.cx = MetricForDpi(SM_CXSMICON, DpiForWindow(tray));
  return spec;
}

} // namespace

Application *Application::instance_ = nullptr;

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
  wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
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
  taskbar::RecoverFromCrash();
  AddTrayIcon();
  taskbar::ApplyPreferences();
  SetTimer(hostWindow_, kEnforceTimerId, 50, nullptr);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  KillTimer(hostWindow_, kEnforceTimerId);
  KillTimer(hostWindow_, kTrayIconTimerId);
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
  TrayIconSpec spec = CurrentTrayIconSpec();
  HICON icon = LoadResourceIcon(hInstance_, spec.id, spec.cx);

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hostWindow_;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
  nid.uCallbackMessage = kTrayCallbackMsg;
  nid.hIcon = icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
  lstrcpyW(nid.szTip, L"Per-Monitor Taskbar");
  if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
    if (icon)
      DestroyIcon(icon);
    return;
  }

  nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &nid);

  if (icon) {
    if (trayIcon_)
      DestroyIcon(trayIcon_);
    trayIcon_ = icon;
    trayIconId_ = spec.id;
    trayIconPx_ = spec.cx;
  }
}

void Application::RefreshTrayIcon() {
  TrayIconSpec spec = CurrentTrayIconSpec();
  if (trayIcon_ && spec.id == trayIconId_ && spec.cx == trayIconPx_)
    return;

  HICON icon = LoadResourceIcon(hInstance_, spec.id, spec.cx);
  if (!icon)
    return;

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hostWindow_;
  nid.uID = kTrayIconId;
  nid.uFlags = NIF_ICON;
  nid.hIcon = icon;
  if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
    DestroyIcon(icon);
    return;
  }

  if (trayIcon_)
    DestroyIcon(trayIcon_);
  trayIcon_ = icon;
  trayIconId_ = spec.id;
  trayIconPx_ = spec.cx;
}

void Application::RemoveTrayIcon() {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hostWindow_;
  nid.uID = kTrayIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);

  if (trayIcon_) {
    DestroyIcon(trayIcon_);
    trayIcon_ = nullptr;
  }
  trayIconId_ = 0;
  trayIconPx_ = 0;
}

void Application::ShowTrayMenu() {
  POINT pt{};
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  if (!menu)
    return;

  AppendMenuW(menu, MF_STRING, 1, L"Settings\u2026");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 3, L"Reset everything");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, 2, L"Exit");

  SetForegroundWindow(hostWindow_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x,
                 pt.y, 0, hostWindow_, nullptr);
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
  Application *app = instance_;

  if (app && app->taskbarCreatedMsg_ && msg == app->taskbarCreatedMsg_) {
    app->AddTrayIcon();
    taskbar::ApplyPreferences();
    return 0;
  }

  switch (msg) {
  case WM_ENDSESSION:
    if (wParam)
      taskbar::RestoreAll();
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;

  case WM_DISPLAYCHANGE:
    SetTimer(hwnd, kDisplayChangeTimerId, 500, nullptr);
    return 0;

  case WM_DPICHANGED:
    if (app)
      app->RefreshTrayIcon();
    return 0;

  case WM_SETTINGCHANGE:
    // Applying preferences on a work-area change would show every taskbar.
    // Enforce() expands the work area again if Explorer reserved the gap.
    // The tray icon still follows the taskbar's DPI, after the move settles.
    if (wParam == SPI_SETWORKAREA) {
      SetTimer(hwnd, kTrayIconTimerId, 500, nullptr);
      return 0;
    }
    SetTimer(hwnd, kDisplayChangeTimerId, 500, nullptr);
    return 0;

  case WM_TIMER:
    if (wParam == kEnforceTimerId) {
      taskbar::Enforce();
      return 0;
    }
    if (wParam == kTrayIconTimerId) {
      KillTimer(hwnd, kTrayIconTimerId);
      if (app)
        app->RefreshTrayIcon();
      return 0;
    }
    if (wParam == kDisplayChangeTimerId) {
      KillTimer(hwnd, kDisplayChangeTimerId);
      taskbar::ApplyPreferences();
      if (app)
        app->RefreshTrayIcon();
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
    case 3:
      taskbar::FactoryReset();
      MessageBoxW(hwnd,
                  L"All taskbars have been restored and preferences cleared.",
                  L"Per-Monitor Taskbar", MB_OK | MB_ICONINFORMATION);
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

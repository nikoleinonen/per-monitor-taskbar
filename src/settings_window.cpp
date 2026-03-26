#include "settings_window.h"

#include "application.h"
#include "resource.h"
#include "taskbar.h"

#include <string>
#include <vector>

namespace settings_window {

namespace {

constexpr const wchar_t* kClassName = L"PerMonitorTaskbarSettings";

UINT GetWindowDpi(HWND hwnd) {
  using Fn = UINT(WINAPI*)(HWND);
  static auto fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn)
    return fn(hwnd);
  HDC hdc = GetDC(nullptr);
  UINT dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
  ReleaseDC(nullptr, hdc);
  return dpi;
}

int Scale(int value, UINT dpi) { return MulDiv(value, dpi, 96); }

struct MonitorRow {
  HWND checkbox = nullptr;
  taskbar::DisplayState display;
};

struct State {
  std::vector<MonitorRow> monitors;
  HWND startupCheckbox = nullptr;
  bool originalStartup = false;
  HFONT font = nullptr;
};

void OnCreate(HWND wnd) {
  auto* st = new State();
  SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

  UINT dpi = GetWindowDpi(wnd);
  auto s = [dpi](int v) { return Scale(v, dpi); };

  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  st->font = CreateFontIndirectW(&ncm.lfMessageFont);

  HINSTANCE hInst = Application::Get()->GetInstance();
  int pad = s(14);
  int contentW = s(440);
  int y = pad;

  HWND title = CreateWindowExW(
      0, L"STATIC", L"Set auto-hide per display:", WS_CHILD | WS_VISIBLE, pad,
      y, contentW, s(20), wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_TITLE)), hInst,
      nullptr);
  SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
  y += s(28);

  auto displays = taskbar::QueryDisplays();
  st->monitors.reserve(displays.size());
  bool anyTaskbar = false;

  for (auto& disp : displays) {
    if (!disp.hasTaskbar)
      continue;

    anyTaskbar = true;
    std::wstring label = L"Auto-hide \u2014 " + disp.displayLabel;
    int ctrlId = IDC_MONITOR_BASE + static_cast<int>(st->monitors.size());

    HWND cb = CreateWindowExW(
        0, L"BUTTON", label.c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, pad, y, contentW, s(24), wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ctrlId)), hInst, nullptr);
    SendMessageW(cb, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    SendMessageW(cb, BM_SETCHECK,
                 disp.autoHide ? BST_CHECKED : BST_UNCHECKED, 0);
    y += s(30);

    MonitorRow row;
    row.checkbox = cb;
    row.display = std::move(disp);
    st->monitors.push_back(std::move(row));
  }

  if (!anyTaskbar) {
    HWND hint = CreateWindowExW(
        0, L"STATIC",
        L"No secondary taskbars found. Enable \"Show my taskbar on all "
        L"displays\" in Windows Settings \u2192 Taskbar.",
        WS_CHILD | WS_VISIBLE, pad, y, contentW, s(40), wnd, nullptr, hInst,
        nullptr);
    SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
    y += s(48);
  }

  y += s(8);

  Application* app = Application::Get();
  st->originalStartup = app ? app->GetStartWithWindows() : false;

  st->startupCheckbox = CreateWindowExW(
      0, L"BUTTON", L"Start with Windows",
      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, pad, y, contentW, s(24), wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STARTUP)), hInst,
      nullptr);
  SendMessageW(st->startupCheckbox, WM_SETFONT,
               reinterpret_cast<WPARAM>(st->font), TRUE);
  SendMessageW(st->startupCheckbox, BM_SETCHECK,
               st->originalStartup ? BST_CHECKED : BST_UNCHECKED, 0);
  y += s(36);

  HWND okBtn = CreateWindowExW(
      0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, pad, y,
      s(90), s(28), wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
  SendMessageW(okBtn, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

  HWND cancelBtn = CreateWindowExW(
      0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
      pad + s(100), y, s(90), s(28), wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
  SendMessageW(cancelBtn, WM_SETFONT, reinterpret_cast<WPARAM>(st->font),
               TRUE);
  y += s(40);

  RECT wr{}, cr{};
  GetWindowRect(wnd, &wr);
  GetClientRect(wnd, &cr);
  int borderW = (wr.right - wr.left) - (cr.right - cr.left);
  int borderH = (wr.bottom - wr.top) - (cr.bottom - cr.top);
  SetWindowPos(wnd, nullptr, 0, 0, contentW + pad * 2 + borderW,
               y + pad + borderH, SWP_NOMOVE | SWP_NOZORDER);
}

void OnOk(HWND wnd) {
  auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
  if (!st)
    return;

  bool startupChecked =
      SendMessageW(st->startupCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (startupChecked != st->originalStartup) {
    if (Application* app = Application::Get())
      app->SetStartWithWindows(startupChecked);
  }

  for (const auto& row : st->monitors) {
    bool checked =
        SendMessageW(row.checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    taskbar::SavePreference(row.display.deviceName, checked);
  }

  taskbar::ApplyPreferences();
  DestroyWindow(wnd);
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    OnCreate(wnd);
    return 0;

  case WM_COMMAND:
    if (LOWORD(wParam) == IDOK) {
      OnOk(wnd);
      return 0;
    }
    if (LOWORD(wParam) == IDCANCEL) {
      DestroyWindow(wnd);
      return 0;
    }
    return 0;

  case WM_DESTROY: {
    auto* st =
        reinterpret_cast<State*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (st) {
      if (st->font)
        DeleteObject(st->font);
      delete st;
    }
    SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
    return 0;
  }

  default:
    return DefWindowProcW(wnd, msg, wParam, lParam);
  }
}

} // namespace

HWND Create(HINSTANCE hInstance) {
  WNDCLASSW existing{};
  if (!GetClassInfoW(hInstance, kClassName, &existing)) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return nullptr;
  }

  HWND wnd = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kClassName, L"Per-Monitor Taskbar",
      WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT,
      CW_USEDEFAULT, 520, 200, nullptr, nullptr, hInstance, nullptr);
  if (!wnd)
    return nullptr;

  ShowWindow(wnd, SW_SHOW);
  UpdateWindow(wnd);
  return wnd;
}

} // namespace settings_window

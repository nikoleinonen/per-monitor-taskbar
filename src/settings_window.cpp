#include "settings_window.h"

#include "application.h"
#include "resource.h"
#include "taskbar.h"

#include <commctrl.h>
#include <string>
#include <vector>

namespace settings_window {

namespace {

constexpr const wchar_t *kClassName = L"PerMonitorTaskbarSettings";

UINT GetWindowDpi(HWND hwnd) {
  using Fn = UINT(WINAPI *)(HWND);
  static auto fn = reinterpret_cast<Fn>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (fn && hwnd) {
    UINT dpi = fn(hwnd);
    if (dpi != 0)
      return dpi;
  }

  HDC hdc = GetDC(nullptr);
  UINT dpi = hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 0;
  if (hdc)
    ReleaseDC(nullptr, hdc);
  return dpi == 0 ? 96 : dpi;
}

int Scale(int value, UINT dpi) { return MulDiv(value, dpi, 96); }

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

struct MonitorRow {
  HWND checkbox = nullptr;
  HWND fullWorkCheckbox = nullptr;
  taskbar::DisplayState display;
};

struct State {
  std::vector<MonitorRow> monitors;
  HWND title = nullptr;
  HWND hint = nullptr;
  HWND startupCheckbox = nullptr;
  HWND applyButton = nullptr;
  bool originalStartup = false;
  HFONT font = nullptr;
  HICON iconBig = nullptr;
  HICON iconSmall = nullptr;
};

void SetControlFont(HWND ctrl, HFONT font) {
  if (ctrl)
    SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

HFONT CreateMessageFont(UINT dpi) {
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);

  using Fn = BOOL(WINAPI *)(UINT, UINT, PVOID, UINT, UINT);
  static auto fn = reinterpret_cast<Fn>(GetProcAddress(
      GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
  if (!fn || !fn(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
      return nullptr;
    HDC hdc = GetDC(nullptr);
    UINT systemDpi =
        hdc ? static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX)) : 0;
    if (hdc)
      ReleaseDC(nullptr, hdc);
    if (systemDpi == 0)
      systemDpi = 96;
    ncm.lfMessageFont.lfHeight =
        MulDiv(ncm.lfMessageFont.lfHeight, static_cast<int>(dpi),
               static_cast<int>(systemDpi));
  }
  return CreateFontIndirectW(&ncm.lfMessageFont);
}

void MoveControl(HWND ctrl, int x, int y, int width, int height) {
  if (ctrl)
    SetWindowPos(ctrl, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

// Positions every control from the 96-DPI layout and fits the frame to it.
// Call after the window is already on the target monitor so the borders match
// that DPI.
void Layout(HWND wnd, State *st, UINT dpi) {
  if (HFONT font = CreateMessageFont(dpi)) {
    HFONT previous = st->font;
    st->font = font;
    SetControlFont(st->title, font);
    SetControlFont(st->hint, font);
    SetControlFont(st->startupCheckbox, font);
    SetControlFont(st->applyButton, font);
    for (const auto &row : st->monitors) {
      SetControlFont(row.checkbox, font);
      SetControlFont(row.fullWorkCheckbox, font);
    }
    if (previous)
      DeleteObject(previous);
  }

  auto s = [dpi](int v) { return Scale(v, dpi); };
  int pad = s(14);
  int contentW = s(440);
  int y = pad;

  MoveControl(st->title, pad, y, contentW, s(20));
  y += s(28);

  for (const auto &row : st->monitors) {
    MoveControl(row.checkbox, pad, y, contentW, s(24));
    y += s(26);
    MoveControl(row.fullWorkCheckbox, pad + s(18), y, contentW - s(18), s(22));
    y += s(28);
  }

  if (st->hint) {
    MoveControl(st->hint, pad, y, contentW, s(40));
    y += s(48);
  }

  y += s(8);
  MoveControl(st->startupCheckbox, pad, y, contentW, s(24));
  y += s(36);
  MoveControl(st->applyButton, pad, y, s(90), s(28));
  y += s(40);

  RECT wr{}, cr{};
  GetWindowRect(wnd, &wr);
  GetClientRect(wnd, &cr);
  int borderW = (wr.right - wr.left) - (cr.right - cr.left);
  int borderH = (wr.bottom - wr.top) - (cr.bottom - cr.top);
  SetWindowPos(wnd, nullptr, 0, 0, contentW + pad * 2 + borderW,
               y + pad + borderH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ApplyWindowIcons(HWND wnd, State *st, UINT dpi) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  if (Application *app = Application::Get())
    instance = app->GetInstance();
  HICON big = LoadResourceIcon(instance, IDI_APP, MetricForDpi(SM_CXICON, dpi));
  HICON small =
      LoadResourceIcon(instance, IDI_APP, MetricForDpi(SM_CXSMICON, dpi));
  if (!big && !small)
    return;

  if (big)
    SendMessageW(wnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
  if (small)
    SendMessageW(wnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small));

  if (big) {
    if (st->iconBig)
      DestroyIcon(st->iconBig);
    st->iconBig = big;
  }
  if (small) {
    if (st->iconSmall)
      DestroyIcon(st->iconSmall);
    st->iconSmall = small;
  }
}

void OnCreate(HWND wnd) {
  auto *st = new State();
  SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

  UINT dpi = GetWindowDpi(wnd);
  ApplyWindowIcons(wnd, st, dpi);

  HINSTANCE hInst = GetModuleHandleW(nullptr);
  if (Application *app = Application::Get())
    hInst = app->GetInstance();

  st->title = CreateWindowExW(
      0, L"STATIC", L"Set auto-hide per display:", WS_CHILD | WS_VISIBLE, 0, 0,
      0, 0, wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_TITLE)), hInst,
      nullptr);

  auto displays = taskbar::QueryDisplays();
  st->monitors.reserve(displays.size());
  bool anyTaskbar = false;

  for (auto &disp : displays) {
    if (!disp.hasTaskbar)
      continue;

    anyTaskbar = true;
    std::wstring label = L"Auto-hide \u2014 " + disp.displayLabel;
    int index = static_cast<int>(st->monitors.size());
    int ctrlId = IDC_MONITOR_BASE + index;
    int fullId = IDC_FULLWORK_BASE + index;

    HWND cb = CreateWindowExW(
        0, L"BUTTON", label.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
        0, 0, 0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ctrlId)),
        hInst, nullptr);
    SendMessageW(cb, BM_SETCHECK, disp.autoHide ? BST_CHECKED : BST_UNCHECKED,
                 0);

    HWND full = CreateWindowExW(
        0, L"BUTTON", L"Maximized windows cover the taskbar area",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, wnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(fullId)), hInst, nullptr);
    SendMessageW(full, BM_SETCHECK,
                 disp.fullWorkArea ? BST_CHECKED : BST_UNCHECKED, 0);
    if (!disp.autoHide)
      EnableWindow(full, FALSE);

    MonitorRow row;
    row.checkbox = cb;
    row.fullWorkCheckbox = full;
    row.display = std::move(disp);
    st->monitors.push_back(std::move(row));
  }

  if (!anyTaskbar) {
    st->hint = CreateWindowExW(
        0, L"STATIC",
        L"No secondary taskbars found. Enable \"Show my taskbar on all displays\" in Windows Settings \u2192 Taskbar.",
        WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, wnd, nullptr, hInst, nullptr);
  }

  Application *app = Application::Get();
  st->originalStartup = app ? app->GetStartWithWindows() : false;

  st->startupCheckbox = CreateWindowExW(
      0, L"BUTTON", L"Start with Windows",
      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, wnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STARTUP)), hInst,
      nullptr);
  SendMessageW(st->startupCheckbox, BM_SETCHECK,
               st->originalStartup ? BST_CHECKED : BST_UNCHECKED, 0);

  st->applyButton = CreateWindowExW(
      0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 0, 0, 0,
      0, wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_APPLY)), hInst,
      nullptr);

  Layout(wnd, st, dpi);
}

void OnApply(HWND wnd) {
  auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
  if (!st)
    return;

  bool startupChecked =
      SendMessageW(st->startupCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (startupChecked != st->originalStartup) {
    if (Application *app = Application::Get()) {
      app->SetStartWithWindows(startupChecked);
      st->originalStartup = startupChecked;
    }
  }

  for (const auto &row : st->monitors) {
    bool checked = SendMessageW(row.checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool fullWork = checked && SendMessageW(row.fullWorkCheckbox, BM_GETCHECK,
                                            0, 0) == BST_CHECKED;
    taskbar::SavePreference(row.display.deviceName, checked);
    taskbar::SaveFullWorkAreaPreference(row.display.deviceName, fullWork);
  }

  taskbar::ApplyPreferences();
}

LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    OnCreate(wnd);
    return 0;

  case WM_COMMAND: {
    auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (st && HIWORD(wParam) == BN_CLICKED) {
      const int index = LOWORD(wParam) - IDC_MONITOR_BASE;
      if (index >= 0 && index < static_cast<int>(st->monitors.size())) {
        const bool hide = SendMessageW(st->monitors[index].checkbox,
                                       BM_GETCHECK, 0, 0) == BST_CHECKED;
        HWND full = st->monitors[index].fullWorkCheckbox;
        EnableWindow(full, hide ? TRUE : FALSE);
        if (!hide)
          SendMessageW(full, BM_SETCHECK, BST_UNCHECKED, 0);
        return 0;
      }
    }
    if (LOWORD(wParam) == IDC_APPLY) {
      OnApply(wnd);
      return 0;
    }
    return 0;
  }

  case WM_DPICHANGED: {
    auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    const auto *suggested = reinterpret_cast<const RECT *>(lParam);
    if (!st || !suggested)
      return DefWindowProcW(wnd, msg, wParam, lParam);

    UINT dpi = LOWORD(wParam);
    if (dpi == 0)
      dpi = 96;

    // Land on the suggested rect first so the frame borders match the new
    // DPI, then fit the client to the scaled controls.
    SendMessageW(wnd, WM_SETREDRAW, FALSE, 0);
    SetWindowPos(wnd, nullptr, suggested->left, suggested->top,
                 suggested->right - suggested->left,
                 suggested->bottom - suggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyWindowIcons(wnd, st, dpi);
    Layout(wnd, st, dpi);
    SendMessageW(wnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(wnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    return 0;
  }

  case WM_DESTROY: {
    auto *st = reinterpret_cast<State *>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (st) {
      SendMessageW(wnd, WM_SETICON, ICON_BIG, 0);
      SendMessageW(wnd, WM_SETICON, ICON_SMALL, 0);
      if (st->iconBig)
        DestroyIcon(st->iconBig);
      if (st->iconSmall)
        DestroyIcon(st->iconSmall);
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
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP));
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
      return nullptr;
  }

  HWND wnd = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kClassName, L"Per-Monitor Taskbar",
      WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
      520, 200, nullptr, nullptr, hInstance, nullptr);
  if (!wnd)
    return nullptr;

  ShowWindow(wnd, SW_SHOW);
  UpdateWindow(wnd);
  return wnd;
}

} // namespace settings_window

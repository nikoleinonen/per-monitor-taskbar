#pragma once

#include <windows.h>

class Application {
public:
  Application();
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  bool Init(HINSTANCE hInstance);
  int Run();

  void ShowSettings();

  HINSTANCE GetInstance() const { return hInstance_; }

  bool GetStartWithWindows() const;
  void SetStartWithWindows(bool enable);

  static Application* Get() { return instance_; }

private:
  static LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam);

  void AddTrayIcon();
  void RemoveTrayIcon();
  void ShowTrayMenu();

  static Application* instance_;

  HINSTANCE hInstance_ = nullptr;
  HWND hostWindow_ = nullptr;
  HWND settingsWindow_ = nullptr;
  UINT taskbarCreatedMsg_ = 0;
};

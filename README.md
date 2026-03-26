# Per-Monitor Taskbar

A lightweight Windows tray utility that lets you enable or disable **taskbar auto-hide on each monitor independently**.

Windows only offers a single global toggle for taskbar auto-hide. This tool works around that limitation by writing per-monitor settings directly into the Explorer registry keys (`StuckRects3` / `MMStuckRects3`) and restarting the shell to apply them.

**Typical use-case:** auto-hide the taskbar on an OLED display (to reduce static burn-in) while keeping it always visible on LCD monitors.

> **Note:** This manipulates undocumented Explorer registry structures. It works on current builds of Windows 10 and 11 but may require updates after major Windows releases.

## Build

Requires **CMake 3.16+** and a Windows C++ toolchain (Visual Studio 2019+ or MinGW-w64).

```
cmake -B build -G "Visual Studio 18 2026" -A x64
$env:Path = "C:\Program Files\CMake\bin;$env:Path"; cmake --build build --config Release 2>&1
```

The binary is placed at `build/Release/per-monitor-taskbar.exe` (path varies by generator).

## Usage

1. Run `per-monitor-taskbar.exe`. A tray icon appears in the notification area.
2. **Left-click** the icon to open Settings, or **right-click** and choose **Settings**.
3. Check **Auto-hide** for each display as desired. Optionally enable **Start with Windows**.
4. Click **OK**. If any auto-hide setting changed, the app will ask to restart Explorer to apply.

Settings are stored in the standard Explorer registry keys under `HKCU`. The "Start with Windows" option registers the exe in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

A second launch of the exe will focus the existing instance's Settings window instead of starting a duplicate.

## How It Works

| Registry key | Scope |
|---|---|
| `HKCU\...\Explorer\StuckRects3` | Primary taskbar (`Settings` binary value) |
| `HKCU\...\Explorer\MMStuckRects3` | Secondary taskbars (one binary value per monitor) |

Byte offset 8 of each binary blob contains the taskbar flags. Bit 1 (`0x02`) is the auto-hide flag. The app reads and writes this bit, then restarts `explorer.exe` so the shell picks up the change.

## Limitations

- **Explorer restart required** -- changing auto-hide settings terminates and relaunches `explorer.exe`. Open File Explorer windows will be closed.
- **Undocumented registry format** -- the `StuckRects3` binary layout is not officially documented by Microsoft and could change in future Windows updates.
- **"Show my taskbar on all displays" must be enabled** -- secondary monitors need their own taskbar for per-monitor control to work.

## License

[MIT](LICENSE)

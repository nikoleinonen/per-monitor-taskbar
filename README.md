# Per-Monitor Taskbar

An extremely lightweight single-purpose Windows tray utility that gives **per-monitor taskbar auto-hide** feature with much faster and more responsive show/hide behavior than the Windows built-in setting.

Windows exposes only a single global taskbar auto-hide toggle. This application applies auto-hide selectively per display by adjusting taskbar window styles and positions at runtime, while keeping the system-wide auto-hide option off. Typical use cases include hiding the taskbar on an OLED panel to reduce burn-in risk while leaving it visible on other monitors.

I made this tool for myself after getting an OLED monitor and decided to make it public in case others find it useful too.

**Disclaimer:** This program modifies Explorer taskbar windows at runtime. It is provided as-is; see the [license](LICENSE) for liability terms. Because it interacts with Windows Explorer internals, it may break with future Windows updates. I actively use this myself and plan to update it if issues arise. If the taskbar becomes stuck in a bad state, see [Troubleshooting](#troubleshooting).

## Screenshots

**Settings** - Per-display auto-hide and optional start with windows toggle.

[![Settings](https://i.imgur.com/r0PxjV9.png)](https://imgur.com/r0PxjV9)

**This tool** - Placing a cursor near the bottom edge; the taskbar shows and hides instantaneously. Maximized windows will not fully extend into the taskbar area. (Fullscreen applications work normally.)

[![Per-monitor auto-hide](https://i.imgur.com/2ZvLjhj.gif)](https://imgur.com/2ZvLjhj)

**Windows built-in auto-hide** - Same interaction, but noticeably slower and less responsive garbage. Maximized windows will fully extend into the taskbar area.

[![Windows default auto-hide comparison](https://i.imgur.com/5ReArXZ.gif)](https://imgur.com/5ReArXZ)

## Requirements

> I personally use:
>
> Windows 11 Version 24H2 (OS Build 26100.7623)

- Windows 10 or later (64-bit recommended; build targets x64).
- **Show taskbar on all displays** must be enabled in Windows Settings if you want a separate taskbar (and thus per-monitor control) on secondary monitors.

## Download

Prebuilt binaries are published on the [Releases](https://github.com/nikoleinonen/per-monitor-taskbar/releases/latest) page. Download `per-monitor-taskbar.exe` and run it; there is no installer.

**SmartScreen:** Unsigned executables often trigger a first-run warning. Click **More info**, then **Run anyway**, or build from source and compare hashes if you prefer. Third-party scanners such as [VirusTotal](https://www.virustotal.com/gui/home/upload) can be used for an additional check.

## Building from source

**Prerequisites**

> I personally use:
>
> CMake 4.3.0
> 
> Visual Studio 18 2026

- [CMake](https://cmake.org/download/) 3.16 or newer
- Windows C++ toolchain - [Visual Studio](https://visualstudio.microsoft.com/downloads/) 2022 or newer (MSVC)

**Building**

1. **Configure** (after cloning or when `CMakeLists.txt` changes):

    ```text
    cmake -B build -G "Visual Studio 18 2026" -A x64
    ```

    Use a generator that matches your installed Visual Studio version. To list available generators:

    ```text
    cmake -G
    ```

2. **Build:**

    ```text
    cmake --build build --config Release
    ```

3. **Output:** 
   
    ```text
   build/Release/per-monitor-taskbar.exe
    ```

## Usage

1. Start `per-monitor-taskbar.exe`. An icon appears in the notification area.
2. **Left-click** the icon to open Settings, or **right-click** for the context menu.
3. Enable **Auto-hide** per monitor as needed. Optionally enable **Start with Windows** so the program automatically starts when you start your pc.
4. Choose **OK**. Changes apply immediately.

The tray menu includes **Reset everything**, which restores taskbar windows, turns off the app’s management state, and removes saved preferences under `HKCU\Software\PerMonitorTaskbar`.

Starting the executable again while an instance is already running brings the existing Settings window to the foreground instead of spawning a second process (mutex: `Local\PerMonitorTaskbar_Singleton`).

## Uninstall

There is no separate uninstaller. To remove the program cleanly:

1. **Right-click** the tray icon and open the context menu.
2. Choose **Reset everything** so taskbars are restored and preferences under `HKCU\Software\PerMonitorTaskbar` are cleared.
3. If you enabled **Start with Windows**, open **Settings** from the tray (left-click), turn that option off, and click **OK** so the startup entry is removed.
4. **Right-click** the tray icon again and choose **Exit**.
5. Delete `per-monitor-taskbar.exe` from your computer.

## How it works

The app turns **off** the global Windows auto-hide state (`SHAppBarMessage` / appbar API) and implements visibility itself:


| Taskbar type | Window class | Hiding approach |
| ------------ | ------------ | --------------- |
| Primary | `Shell_TrayWnd` | Extended styles `WS_EX_LAYERED` and `WS_EX_TRANSPARENT` with alpha 0 so the bar is invisible and click-through. The shell resists moving the primary bar, so transparency is used instead of repositioning. |
| Secondary | `Shell_SecondaryTrayWnd` | Positioned so only about **2 px** remains visible at the bottom edge of the monitor, similar to native auto-hide. |


A **50 ms** timer polls the cursor. If the pointer lies in a **48 px** hot zone above the bottom edge of a monitor whose taskbar is managed, the bar is shown (styles restored and redrawn). When the cursor leaves that zone and is not over the taskbar, the bar is hidden again.

Preferences are stored in the registry:

- `HKCU\Software\PerMonitorTaskbar\Monitors` - DWORD per display device name (`1` = auto-hide on, `0` = off).

### Crash recovery

Before altering the primary taskbar, the app records a dirty flag and the original extended style under `HKCU\Software\PerMonitorTaskbar` (`PrimaryManaged`, `PrimaryOrigExStyle`). On the next startup, if the dirty flag is still set, those values are used to restore the primary taskbar.

Additional safeguards:

- `SetUnhandledExceptionFilter` calls full taskbar restore before the process terminates on an unhandled exception.
- `WM_ENDSESSION` restores taskbars on shutdown or logoff when applicable.
- **Reset everything** performs a forced restore of the primary bar and deletes the app’s registry tree.

## Limitations

- **Work area:** On monitors where auto-hide is active through this tool, maximized windows will not reclaim the full height; a thin, taskbar-height strip will remain at the bottom. I find this more of a feature than a bug, but opinions may vary. This does not apply to fullscreen applications, which will work as usual.
- **Multi-taskbar setup:** Per-monitor behavior requires a taskbar on each display (Windows **Show taskbar on all displays**).
- **Force-kill and removed binary:** If the process is terminated forcibly and the executable is deleted before a normal restart of the app, the primary taskbar can remain invisible until Explorer is restarted or the system is rebooted.

## Troubleshooting

**Taskbar missing or not responding**

1. Open Task Manager (**Ctrl+Shift+Esc**).
2. Locate **Windows Explorer**.
3. Right-click it and choose **Restart**.

Explorer recreates taskbar windows, clearing leftover extended styles from this utility.

If that does not help, try restarting your computer.

## License

[MIT License](LICENSE)
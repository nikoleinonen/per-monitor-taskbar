# Per-Monitor Taskbar

A lightweight Windows tray utility that lets you enable or disable **taskbar auto-hide on each monitor independently** - and the show/hide is significantly more responsive than the built-in Windows taskbar auto-hide.

Windows only offers a single global toggle for taskbar auto-hide. This tool works around that limitation so you can, for example, auto-hide the taskbar on an OLED display (to avoid static burn-in) while keeping it always visible on LCD monitors.

I built this with AI for my own setup due to getting an OLED monitor and decided to make it public in case others find it useful. **Use at your own risk.** This modifies taskbar window styles at runtime. I am not liable for any issues or damages - if something breaks, see the [Troubleshooting](#troubleshooting) section below if that helps!

## Screenshots & demo

**Settings panel** — configure auto-hide per monitor from the tray icon.

[![Settings panel](https://i.imgur.com/r0PxjV9.png)](https://imgur.com/r0PxjV9)

**Per-Monitor Taskbar** — hovering the auto-hide strip: taskbar shows and hides instantaneously. (applications does not fully fill the monitor)

[![Responsive per-monitor auto-hide](https://i.imgur.com/2ZvLjhj.gif)](https://imgur.com/2ZvLjhj)

**Windows built-in auto-hide** — same kind of interaction, but noticeably slower and less responsive garbage. (applications fully fills the monitor)

[![Windows default auto-hide comparison](https://i.imgur.com/5ReArXZ.gif)](https://imgur.com/5ReArXZ)

## Build

Requires **CMake 3.16+** and a Windows C++ toolchain (Visual Studio 2022+ or MinGW-w64). If you do not have CMake yet, get it from the [CMake download page](https://cmake.org/download/) and run the **Windows x64 Installer**. Any recent release is probably fine; this project is built and tested with **CMake 4.3.0**, but **3.16+** is all that CMakeLists requires.

CMake splits the work into two steps:

1. **Configure** (run once, or again after changing `CMakeLists.txt` or generator): creates the `build` folder and generates Visual Studio project files inside it.

   ```text
   cmake -B build -G "Visual Studio 18 2026" -A x64
   ```

2. **Build** (run whenever you change source code): invokes the compiler and produces the `.exe`.

   ```text
   cmake --build build --config Release
   ```

The `-G` name must match your installed Visual Studio version (for example **Visual Studio 17 2022** for VS 2022). If unsure, run `cmake -G` to list generators.

The binary is placed at `build/Release/per-monitor-taskbar.exe`.

Feel free to make a shortcut to your desktop or what ever to this .exe to have an easy access.

## Usage

1. Run `per-monitor-taskbar.exe`. A tray icon appears in the notification area.
2. **Left-click** the icon to open Settings, or **right-click** for the menu.
3. Check **Auto-hide** for each display as desired. Optionally enable **Start with Windows** (recommended).
4. Click **OK**. Changes take effect immediately.

The tray menu also offers **Reset everything**, which restores all taskbars to their default state and clears all saved preferences.

A second launch of the exe will focus the existing instance's Settings window instead of starting a duplicate.

## How It Works

The app keeps the Windows global auto-hide toggle **OFF** and implements per-monitor auto-hide itself:


| Monitor                                  | Hiding mechanism                                                                                                                                                                          |
| ---------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Primary** (`Shell_TrayWnd`)            | Made transparent (`WS_EX_LAYERED`, alpha 0) and click-through (`WS_EX_TRANSPARENT`). The shell protects the primary taskbar's position, so transparency is used instead of repositioning. |
| **Secondary** (`Shell_SecondaryTrayWnd`) | Repositioned off-screen (2 px visible at the bottom edge), matching native auto-hide behaviour.                                                                                           |


A 50 ms timer monitors cursor position. When the cursor enters the bottom edge of a hidden monitor, the taskbar is fully restored (original window styles, full redraw). When the cursor leaves, it hides again.

Per-monitor preferences are stored in `HKCU\Software\PerMonitorTaskbar\Monitors` as DWORD values keyed by display device name.

### Crash recovery

Before modifying the primary taskbar, the app writes a dirty flag and the original extended style to the registry. On the next launch, if the flag is still set (indicating a previous crash or force-kill), the taskbar is automatically restored. Additional safety layers:

- **`SetUnhandledExceptionFilter`** restores taskbars on unhandled exceptions.
- **`WM_ENDSESSION`** restores taskbars on system shutdown / logoff.
- **Reset everything** in the tray menu performs a brute-force restore as a last resort.

## Limitations

- **Work area gap** - on monitors with auto-hide enabled, maximized windows don't reclaim the taskbar space (a thin gap remains at the bottom). Personally I find this as a feature more than a bug.
- **"Show my taskbar on all displays" must be enabled** - secondary monitors need their own taskbar window for per-monitor control to work. (If that is what you want)
- **Force-kill + uninstall edge case** - if the app is killed via Task Manager and the exe is deleted before re-launching, the primary taskbar stays invisible until you restart Explorer or reboot.

## Troubleshooting

**Taskbar disappeared and won't come back?** Restart Windows Explorer:

1. Press **Ctrl + Shift + Esc** to open Task Manager.
2. Find **Windows Explorer** in the process list.
3. Right-click it and choose **Restart**.

This forces Explorer to recreate all taskbar windows from scratch, which clears any leftover styles the app may have applied. (Hopefully)

## License

[MIT](LICENSE)
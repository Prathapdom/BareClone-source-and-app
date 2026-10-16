# BareClone — working notes for Claude

BareClone is a native C++17 / Win32 file-content search app for Windows (an independent BareGrep-style tool). It has no frameworks or runtime dependencies: one portable, statically linked x64 exe. The user-facing docs are in README.md, and STORE.md covers MSIX packaging. VALIDATION.md records what has and hasn't been verified.

This folder is a local git repository (no remote). `dist/` build outputs are gitignored.

## Files

| File | What it is |
|---|---|
| `search.hpp` | Header-only search engine (`namespace search`): `Options`, `Result`, `Stats`, `Engine` (4 worker threads, bounded queues, literal/regex/whole-word/case matching, encoding detection, result caps). |
| `main.cpp` | The whole GUI plus the worker-process entry point (`--worker`). |
| `tests.cpp` | Engine tests → `dist/search-tests.exe`. |
| `ui-tests.cpp` | Hidden-window GUI integration tests that drive the real exe through window messages → `dist/ui-tests.exe <path-to-BareClone.exe>`. |
| `benchmark.cpp` | Standalone synthetic benchmark (not part of the build). |
| `app.rc`, `app.manifest` | Icon, version info, comctl32 v6, per-monitor DPI, long paths. |
| `build.ps1` / `Build.cmd` | The real build. `CMakeLists.txt` exists but has never been tested. |
| `packaging/` | MSIX template, asset generator (`Make-Assets.ps1`, run by build.ps1), icons. |

## Build & test

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```
- The compiler is TDM-GCC 9.2 at `F:\Software\TDM-GCC-64\bin\g++.exe`. It links `comctl32 ole32 uuid shell32 comdlg32 gdi32 uxtheme dwmapi`. If you add a library, update both `build.ps1` and `CMakeLists.txt`.
- build.ps1 compiles the app and runs both test suites. It fails if anything fails. The build must stay free of `-Wall -Wextra` warnings.
- **If BareClone.exe is running, the link fails with "Permission denied".** Don't kill the user's instance. Ask them to close it, or build to a temporary name (e.g. `dist/BareClone-new.exe`) with the same g++ line and run `ui-tests.exe` against that.
- Python on this machine is `py -3` (`python` is Python 2).

## Architecture (main.cpp)

- **Process split:** each search spawns `BareClone.exe --worker` in a kill-on-close job object. Options go over the stdin pipe, and results come back over stdout using the `wire::` framing (tag 1 = result, tag 2 = stats snapshot). Stop = `TerminateJobObject`, which works even against a runaway regex. A UI-side reader thread fills `pending`; `tick()` (60 ms timer) moves up to 1,000 rows per tick into `rows`.
- **UI:** raw Win32 controls created in `WM_CREATE` via `add()`, with positions in 96-DPI units scaled by `px()`. `layout()` repositions everything on `WM_SIZE`. Results are a virtual `LVS_OWNERDATA` list view (`LVN_GETDISPINFOW`).
- **Control IDs** (enum near the top): `Query=101, Root, Includes, Excludes, Browse, Search(106), Stop, Recursive, Case, Whole, Regex(111), Export, Open, Reveal, Copy, Results(116), Preview, RecentMenu, ThemeMenu`. Static labels are 300–307 (307 = status label, which the tests read). Menu command IDs: `RecentFirst=400..409, RecentClear=420, ThemeSystem=430, ThemeLight, ThemeDark, ThemeAmoled(433)`. **Only append to these enums.** ui-tests.cpp hard-codes the numbers.
- **Settings:** registry `HKCU\Software\BareClone` (`readSetting` / `writeSetting`). The `BARECLONE_SETTINGS_KEY` env var overrides the key; ui-tests uses it so tests never touch real user settings.
  - `Theme` DWORD: 0 System, 1 Light, 2 Dark, 3 AMOLED.
  - `Recent\0..9`: REG_SZ values `root␟query␟include␟exclude␟flags`, where ␟ = U+001F and flags = 1 recursive | 2 case | 4 whole | 8 regex.
- **Recent searches** (`loadRecents` / `saveRecents` / `addRecent` / `applyRecent`): the most recent 10, with duplicates removed. A search is recorded in `startSearch()` once the worker launches. `applyRecent` restores every field and checkbox, then calls `startSearch()`. Menu items are greyed out while a search or export is running.
- **Themes** (`applyTheme`, `Palette` structs `lightPalette` / `darkPalette` / `amoledPalette`):
  - Light = the original look: themed controls with `WS_EX_CLIENTEDGE`.
  - Dark/AMOLED: client edges are removed. The main window paints rounded field outlines (`fields` vector, `paintFields`, accent outline when focused), and `move()` insets the child edit or list inside each outline.
  - Push buttons and checkboxes are fully custom-drawn via `NM_CUSTOMDRAW` (`drawButton`). Search is the accent "primary" button.
  - The list header is drawn in `listProc` (the list view is subclassed because the header notifies the list, not the main window). Selected rows are recoloured in the list's `NM_CUSTOMDRAW`.
  - `DarkMode_Explorer` / `DarkMode_ItemsView` window themes give dark scrollbars. DWM attributes 20/19 set the dark title bar; 35/34 set the black caption and border on Win11 in AMOLED mode. Undocumented uxtheme ordinals 135/136 make popup menus dark (Win10 build 18362+ only).
  - System mode follows `AppsUseLightTheme` and re-applies on `WM_SETTINGCHANGE "ImmersiveColorSet"`. High contrast forces Light.
  - The main window has `WS_CLIPCHILDREN`, which field painting relies on.

## Code style

The existing code is deliberately dense: several statements per line, short names, minimal comments that explain *why* rather than *what*. Match it. Don't reformat existing code.

## Gotchas learned the hard way

- **In tests, read an edit control's text with `SendMessageW(WM_GETTEXT)`, not `GetWindowTextW`.** Across processes, `GetWindowTextW` returns the creation-time caption, not the current edit text. The ui-tests helper `field(id)` does this; `label(id)` is only for statics.
- **Never send pointer-carrying messages across processes** (e.g. `ListView_SetItemState`, `LVM_GETITEM`). Only system-marshalled ones like `WM_SETTEXT` / `WM_GETTEXT` are safe. Others crash the target app.
- In the Bash tool, backslashes in heredocs and sed replacements get mangled (e.g. `\\B`, `\n`). Edit C++ string literals with the Edit/Write tools, not sed.
- PrintWindow screenshots work for visual checks. Launch the exe from a small helper with `BARECLONE_SETTINGS_KEY` set, send `WM_COMMAND 431–433` to switch themes, then `PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT=2)`. Convert the BMP to PNG with System.Drawing in PowerShell.

## Not verified yet

- Dark popup menus (couldn't screenshot them).
- Message boxes and folder/save dialogs stay light in dark themes, by design.
- CMake/MSVC build, MSIX packaging/signing, Store certification, clean-machine Windows 10 testing.

## Ideas / possible next features

Incremental search while typing, recent folders in a combobox, sorting result columns, opening files in a user-chosen editor at the matched line, named saved searches, multi-select copy/export.

# BareClone

Native C++17 / Win32 file-content search for Windows. No web runtime, telemetry, services, or account required.

## Run

Open **dist/BareClone.exe**. It is a portable Windows x64 executable, with the C++ runtime linked statically. No installation or .NET runtime is needed.

This session could create files only under the profile's temporary directory, despite the configured writable profile root. To move the project into the requested directory, run this from PowerShell under your normal Windows account:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "C:\Users\vr18644\AppData\Local\Temp\bareclone-build\Install-ToRequestedFolder.ps1"
```

The copy script defaults to `C:\Users\vr18644\bareclone`, copies the executable and source, and refuses to overwrite a nonempty destination. Copy the project out of Temp before Windows cleans temporary files.

## Search

1. Enter text, choose a folder, and press Enter in the search field or click Search.
2. Use `*.cpp;*.hpp;*.txt` to search multiple filename patterns. `*` searches all filenames. Patterns match names, not full paths.
3. Exclusions also match names. Defaults skip `.git`, `node_modules`, `.svn`, and `.hg`. Clear exclusions to include those folders.
4. Toggle subfolders, case matching, whole words, or ECMAScript regular expressions. Example regex: `error|warning`, `^ERROR`, or `status=[45][0-9]{2}`.
5. Select a result to preview its matching line. Double-click or Open file opens the file in Notepad. Show in folder reveals it in Explorer.
6. Copy result copies `path:line: text`; Ctrl+C works in the results list. Export CSV saves all displayed results after searching completes.

7. **Recent ▾** (Alt+R) lists your last 10 searches, newest first. Picking one restores its text, folder, patterns, exclusions and options, then runs it again. *Clear recent searches* empties the list.
8. **Theme ▾** (Alt+H) switches between *Use system setting* (the default, which follows Windows' app mode), *Light*, *Dark* and *AMOLED black*. AMOLED black uses a pure `#000000` background, fields and results list, and colours the title bar black on Windows 11. High-contrast mode always uses the light, system-coloured theme.

Recent searches and the theme choice are saved per user under `HKCU\Software\BareClone`. Nothing is written next to the executable.

F5 starts a search. Escape stops it. Result order follows parallel completion and is not sorted. Line numbers start at 1. Each matching line appears once, even if it contains several matches.

## Performance design

- Native Win32 controls and a virtual list view (`LVS_OWNERDATA`), rather than one UI widget per result.
- Four parallel file-scanning workers with 256 KiB sequential reads.
- Bounded file queue (256 paths) and IPC queue (2,048 result lines).
- Batched UI updates every 60 ms, at most 1,000 new rows per tick.
- Search runs in a child process in a kill-on-close Windows job. Stop can terminate a costly regex or blocked filesystem read without waiting for the matcher.
- Literal search has an ASCII case-folding fast path, with invariant Windows Unicode casing for non-ASCII text.
- CSV export runs on a background thread.

The `benchmark-results.txt` file records a synthetic **warm-cache engine-only** run over 128 generated 1 MiB text files, four workers, 128 matching lines. It excludes UI, process startup, and fixture generation. It is not a disk-speed claim or comparison with BareGrep. Rerun the benchmark on representative logs and storage for a useful comparison.

## Current limits

- Plain text content only: no PDF/Office extraction, archives, or indexed database.
- UTF-8 (with or without BOM), BOM-marked UTF-16 LE/BE, and fallback Windows ANSI decoding. LF and CRLF line endings are supported. BOM-less UTF-16 is treated as binary.
- Binary detection looks for NUL bytes in the first 256 KiB, except BOM-marked UTF-16.
- Reparse points are skipped, including symbolic links, junctions, and some cloud placeholders. This avoids loops and unexpected downloads.
- Results stop at 100,000 rows or approximately 64 MiB of result string storage, whichever comes first. Queue/vector/allocator overhead is additional.
- Displayed/exported matching text is limited to the first 2,000 characters per line. Literal search still checks the full supported line before truncation.
- Lines larger than 1 MiB are skipped; regex lines over 4,096 UTF-16 code units are skipped. Skipped oversized lines and access/read errors appear in the final status.
- Regex uses the toolchain's ECMAScript `std::wregex`; it is not PCRE. Expensive expressions can be stopped, but are not guaranteed to have bounded execution time. Regex Unicode casing follows the standard library implementation.
- Matching-line preview only; opening in Notepad shows the complete file. No content editing, named saved searches, or incremental search while typing yet. Message boxes and the folder/save dialogs keep the Windows default (light) styling in dark themes.
- Built and tested as x64. MSIX packaging, ARM64, and Store certification have not been run in this environment.

## Build and verify

GCC/MinGW build (the local script defaults to the installed TDM-GCC compiler):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
```

Pass `-Compiler` to use another MinGW `g++.exe`. The script generates branding assets, compiles the app, and runs engine and hidden-window UI integration tests. Tests cover search correctness, Unicode, filters/exclusions, result bounds, worker IPC, invalid regex, cancellation, restart, and clean shutdown.

CMake support is also included for a current Visual Studio C++ or MinGW toolchain. Generate the assets with `packaging/Make-Assets.ps1` before building. The Visual Studio/CMake route is provided but has not been tested here.

For the standalone benchmark:

```powershell
g++ -std=c++17 -O3 -static benchmark.cpp -o dist/benchmark.exe
.\dist\benchmark.exe
```

## Microsoft Store

See [STORE.md](STORE.md) for MSIX packaging and the remaining submission steps. Packaging templates are included; this is not yet a certified Store submission.

BareClone is an independent implementation. It includes no BareGrep code, branding, or visual assets.

## License

[MIT](LICENSE).

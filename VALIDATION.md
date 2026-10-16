# Validation — 2026-10-16

Built Windows x64 release executable with installed MinGW/TDM GCC 9.2.0, C++17, O3, static C++ runtime. Import inspection found Windows system DLLs only (COMCTL32, comdlg32, GDI32, KERNEL32, msvcrt, ole32, SHELL32, USER32).

Passed engine tests: literal matching, case sensitivity, whole words, wildcard patterns, recursive/non-recursive traversal, excluded folders, binary detection, UTF-8 BOM, UTF-16 LE, Unicode literal casing/word classification, ECMAScript regex and invalid regex, cancellation, oversized-line recovery, and 100,000-row result limit.

Passed hidden-window GUI integration tests: application startup, actual child-process IPC, virtual-list row counts, invalid regex recovery, UI message responsiveness during search, Stop, restart with fresh results, and clean shutdown.

PowerShell scripts parse successfully. MSIX template parses as well-formed XML. Generated package logos and app icon successfully.

Synthetic warm-cache benchmark: 128 files × 1 MiB, four workers, 128 matching lines. Case-insensitive: 0.273212 s / 468.501 MiB/s. Case-sensitive: 0.219691 s / 582.637 MiB/s. This measures engine throughput, not full UI or cold disk performance. See benchmark.cpp and benchmark-results.txt.

Not verified: manual visual review (sandbox screenshot capture returned black), clipboard/Notepad/Explorer integration, CSV save dialog, CMake/MSVC build, Windows 10 clean-machine testing, MSIX generation/installation/signing, or Windows App Certification Kit/Store submission. The target profile-root folder was denied by Windows ACLs; artifacts were created in the permitted temporary folder instead.

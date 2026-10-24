# Windows build environment

M1.1a tools verified on 2026-10-19; instrument build below. Cross-platform CI/packages: [ci.md](ci.md). See [dependencies.md](dependencies.md) for licenses and [../tools/windows-toolchain.json](../tools/windows-toolchain.json) for pinned tool downloads.

## Installed and verified

| Tool | Observed version / location |
|---|---|
| Visual Studio Community 2022 | 17.14.42, installation 17.14.37806.7; `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| MSVC v143 x64/x86 | cl 19.44.35229.0, tools directory 14.44.35207 |
| Windows SDK | 10.0.26100.0 |
| CMake / CTest | 4.4.4; `build/tools/cmake-4.4.4-windows-x86_64/bin` |
| pluginval | 1.0.4; `build/tools/pluginval-1.0.4/pluginval.exe` |

- Microsoft installer exited 0; installation reports complete/launchable, with no reboot required. No restart was performed, and no Visual Studio setup process remained at final inspection. Windows Installer (`msiserver`/`msiexec`) is a separate OS service; it was not stopped or reconfigured.
- Verified a valid Microsoft Corporation Authenticode signature before execution. The official `https://aka.ms/vs/17/release/vs_community.exe` redirected to the fixed Microsoft download URL now recorded in the manifest. The signed bootstrapper version is 17.14.37806.7.
- Explicit selections are recorded in [windows.vsconfig](../tools/windows.vsconfig): Desktop development with C++, MSVC 14.44 x64/x86, and Windows SDK 26100. Neither recommended nor optional component groups were enabled.
- Automatic required components include the Community/Core Editor shell, C++ IDE support, MSBuild, Roslyn compiler, text templating, VC runtime redistributables, supporting .NET runtimes and SDK packages. No gaming, web, Python, Azure, mobile or Linux workloads were selected. The SDK carries some architecture-specific files as package dependencies; the initial build target remains x64.
- The installer reports a system-drive increase of 10,959,867,904 bytes (about 10.2 GiB), including dependencies/caches. Community includes an IDE shell, so this is larger than a bare command-line compiler. The owner does not need to open the IDE to build the project. Nothing has been uninstalled or cleaned up.
- Portable archives/extracted binaries remain under ignored `build/`; no global PATH edits or MSYS2 changes. No paid edition, purchase or trial was used. Ninja was not needed or installed.

## Reproduce / verify

Follow [storage maintenance](storage.md) at session boundaries and after verification. Keep tools/C++ sources/text evidence; remove optional audio/archives, never `build/` wholesale.

Use PowerShell 7. On a fresh Windows machine, the opt-in system installer command requires administrator access:

```powershell
pwsh -NoProfile -File tools/setup-windows.ps1 -InstallVisualStudio
pwsh -NoProfile -File tools/check-windows-toolchain.ps1
```

Without `-InstallVisualStudio`, setup only verifies/downloads/extracts portable CMake and pluginval. It validates pinned hashes, checks Microsoft's installer signature when installation is requested, selects the saved configuration, and never automatically restarts. Binary downloads and runtime environment changes are development-only.

The readiness script builds [toolchain_probe](../tools/toolchain_probe/CMakeLists.txt), which compiles C++17, links/calls a Windows SDK function, checks x64, and runs through CTest. It also waits for pluginval `--version` to exit. Observed result: configure/build succeeded, 1/1 CTest passed, pluginval printed 1.0.4 and exited 0. Actual Ironstack pluginval evidence is in [state-editor.md](state-editor.md) and [ci.md](ci.md).

## Important generator discovery workaround

CMake's `Visual Studio 17 2022` generator could not discover this otherwise complete instance, even with its path/version supplied. Do not reinstall Visual Studio or add modules merely to address that discovery failure. The verified path uses existing `VsDevCmd.bat` with `-arch=x64 -host_arch=x64 -vcvars_ver=14.44`, then CMake's `NMake Makefiles` generator. Configuration, compilation and CTest all run inside that child environment, leaving the parent/global PATH unchanged.

`tools/build-windows.ps1` reuses that child environment, targeting the root project in `build/windows-release` with `-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON`. NMake does not provide the parallel behavior of Ninja. Do not mix generators in one build directory. If sandbox execution stalls at compiler detection, stop that build and rerun with approved execution outside the sandbox; do not reinstall tools.

## Instrument build (M1.2/M1.3)

- Glaze was inspected in M1.1: `C:\Code\glaze` contains specifications only, no CMake/source/shared API/Git metadata; no sibling files changed. Existing MSYS2 GCC 14.2 remains intact, but JUCE explicitly rejects MinGW. No `C:\Code\dsp-core` exists.
- `shared/dsp-core` is a standalone headless interface CMake subproject, target `dsp_core` / alias `dsp-core::dsp-core`, now with header-only oscillator/ramp/envelope primitives. Instrument DSP stays in `src/dsp`. Future Glaze can FetchContent a pinned Ironstack commit with SOURCE_SUBDIR `shared/dsp-core`, without duplicated code or a mandatory sibling checkout.
- Root CMake requires 3.24+, pins JUCE 9.0.4, centralizes product/version 0.1.0, and builds C++17 VST3/Standalone synths with MIDI input/stereo output. The prototype editor exposes master Output/Panic only; [state/editor checks](state-editor.md) cover M1.4. Manufacturer/plugin codes are `Hcct` / `Irsk`.
- Browser/curl, Windows MIDI Services, VST2 replacement, ASIO and unused codecs are disabled; [JUCE integration](juce-runtime.md) lists notices. No instrument-runtime fetches occur.
- Owner confirmed individual JUCE Starter eligibility on 2026-10-19. Starter is the selected no-cost integration route on that basis; recheck before release. Project distribution license remains a separate pre-release decision; do not apply AGPL automatically.

```powershell
pwsh -NoProfile -File tools/build-windows.ps1
# Offline alternative: prepopulate the exact pinned JUCE commit, then:
pwsh -NoProfile -File tools/build-windows.ps1 -JuceSource C:/path/to/pinned/JUCE
```

The default build downloads the commit archive and verifies its SHA-256. `-JuceSource` bypasses downloading/hash checks: use the exact pin from [juce-runtime.md](juce-runtime.md). This session used the verified official archive extracted under ignored `build/vendor/`. Portable tools and all generated artifacts stay in `build/`; nothing is installed to a host plugin directory automatically.

Outputs: `build/windows-release/Ironstack_artefacts/Release/Standalone/Ironstack.exe` and `build/windows-release/Ironstack_artefacts/Release/VST3/Ironstack.vst3/`. CTest covers idle/buffer, oscillator/MIDI and processor/editor/state checks. Standard-sequence rendering starts in M2. Pluginval strictness 5 has passed; DAW listening and a verified milestone release remain later gates. No version bump/release is due for this partial milestone.

For controlled drive diagnostics, build target `alias_tool` in the same developer environment, then run `build/windows-release/alias_tool.exe`. It writes JSON to stdout and no audio files; exit0 means measurement completed, not alias acceptance. Use `--help` for scope.

## M1.2 verification — 2026-10-19

- The offline-source command above completed the Windows x64 Release build and CTest 1/1; final incremental rebuild/CTest also passed. The generated VST3 manifest identifies Ironstack 0.1.0, VST 3.8.0, Instrument/Synth. No pluginval or DAW acceptance is claimed.
- Standalone SHA-256: `445277fb838c332b8b8584595ea999b70a2720e77312772e9c9036a98ad7a827`.
- VST3 inner binary SHA-256: `8015531449a6a095fc60f69b205d2aee6fb5275f4340597ba812f59f6fa039bf`.
- `shared/dsp-core` separately configured/built with MSVC/NMake in `build/dsp-core-probe`, without JUCE. Script syntax and documentation size guard passed. Local logs: `build/m1.2-build.log`, `build/m1.2-final-build.log` (ignored).

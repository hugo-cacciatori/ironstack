# Dependency register

M1.2 integration, 2026-10-19. Proprietary software is allowed at no additional cost. See [build.md](build.md) for builds and [juce-runtime.md](juce-runtime.md) for the enabled module inventory, archive digest and retained redistribution notices.

M1.5 action pins, runner/tool eligibility and platform-system library licenses are registered in [ci.md](ci.md). These tools/system libraries are not bundled; Linux system notices and exact environment inventory accompany CI packages.

| Component | Pin / observed version | Purpose and license |
|---|---|---|
| JUCE | 9.0.4, commit `3fb5cbc0340758596dac39f7c8de47d562b4896e` | Integrated under free Starter; individual eligibility confirmed by owner on 2026-10-19, EULA rechecked at integration. AGPL is not selected. [Pinned license](https://github.com/juce-framework/JUCE/blob/3fb5cbc0340758596dac39f7c8de47d562b4896e/LICENSE.md). |
| Bundled VST3 SDK | Version bundled in the JUCE commit above | MIT at this exact pin; retain its notice. No separate SDK download or VST2 dependency. [Pinned SDK license](https://github.com/juce-framework/JUCE/blob/3fb5cbc0340758596dac39f7c8de47d562b4896e/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt). |
| CMake + CTest | 4.4.4, portable Windows x64 package | Installed under build/tools; configure/build/CTest probe passed. BSD 3-Clause for CMake itself, with bundled notices if redistributed. [License](https://cmake.org/licensing/), [release](https://github.com/Kitware/CMake/releases/tag/v4.4.4). |
| Ninja | 1.13.2 proposed Windows package | Optional build runner, Apache-2.0. [Pinned license](https://github.com/ninja-build/ninja/blob/v1.13.2/COPYING). Visual Studio generator does not require it. Not installed. |
| pluginval | 1.0.4, commit `ed19c2c16b57a6d94db391bea3ef4a80b769d5bf` | External validator; M1.4 strictness 5 passed locally. GPLv3; do not link or bundle with the instrument. [License](https://github.com/Tracktion/pluginval/blob/v1.0.4/LICENSE), [release](https://github.com/Tracktion/pluginval/releases/tag/v1.0.4). |
| Microsoft Windows toolchain | VS Community 17.14.42 / installation 17.14.37806.7; cl 19.44.35229.0; SDK 10.0.26100.0 | Installed, C++17/Win32 probe passed using bundled NMake. No-cost individual use under Microsoft terms; not redistributed as part of Ironstack. [Community usage](https://visualstudio.microsoft.com/vs/community/), [versions](https://learn.microsoft.com/en-us/visualstudio/releases/2022/release-history). Exact acquisition pins/components: [tool manifest](../tools/windows-toolchain.json), [configuration](../tools/windows.vsconfig). |
| Existing GCC / MinGW-w64 | GCC package `14.2.0-2`, binutils 2.44, MinGW CRT/headers 12.0.0 snapshot | Local inspection only: GCC GPL-3.0-or-later; libgcc/libstdc++ have GCC Runtime Library Exception 3.1; CRT package ZPL-2.1, winpthreads MIT/BSD-3-Clause-Clear per installed package metadata. Probe compiled and ran, but JUCE rejects MinGW. [Runtime license](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html). Not the proposed plugin toolchain. |

## Acquisition verification

Use exact tags/commits, never a moving `latest` URL. Official GitHub release metadata queried during the audit gives these Windows archive SHA-256 values:

```text
cmake-4.4.4-windows-x86_64.zip
bace36e94b31c68ab6fa295f26dfa11219e0701cf7c94b0284a7d1cb13dac536
ninja-win.zip (v1.13.2)
07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65
```

The [acquisition manifest](../tools/windows-toolchain.json) also pins the fixed Microsoft installer URL/hash (valid Microsoft Corporation Authenticode signature) and pluginval archive hash `c08e61ce3b96db41636f8ec7e76f4c7e2c13ebdac7fa1b5a1f52b4f32ec715ab`. pluginval is unsigned and its release supplied no publisher digest: that hash is locally recorded from the official tagged asset, not independently publisher-verified. Downloads/extracted tools stay ignored; no vendor binaries are committed.

## No-cost licensing and distribution

- M2.1 renderer uses only existing pinned JUCE audio_basics/audio_formats/core modules (MIDI and float WAV) plus the C++ standard library; existing Starter terms/notices apply. No GUI/device modules or additional codecs/dependencies are enabled by its target.
- Preferred JUCE route is Starter, which is free under the [JUCE 9 terms](https://juce.com/legal/juce-9-licence/). Its $20,000 limit uses the previous 12 months: for an individual, revenue/funding generated through JUCE use; for a company, revenue/funding of the entity and affiliates from all sources. Eligibility is not established by a public repository or a free plugin. Verify before integration and distribution.
- If Starter eligibility fails, never purchase or automatically upgrade. AGPLv3 remains a no-fee option with source-distribution obligations, requiring the owner's explicit choice; otherwise investigate another no-cost framework. A commercial JUCE license may be required for non-AGPL use beyond free eligibility, which is outside this project's budget.
- Owner chose free binary use with source modification/redistribution rights reserved on 2026-10-19. [Project permissions](../LICENSE.txt) accompany binaries as PROJECT-LICENSE.txt; this is not MIT/AGPL. Third-party rights/notices remain intact.
- Use free standard CI capacity only, with no paid runners/overages. Paid signing certificates, Apple developer memberships and expiring trials must not be prerequisites; offer legally distributable unsigned packages with accurate installation instructions where appropriate.
- The pinned [JUCE SPDX inventory](https://github.com/juce-framework/JUCE/blob/3fb5cbc0340758596dac39f7c8de47d562b4896e/JUCE.spdx.json) was reviewed for M1.2's Windows module set; see [integration register](juce-runtime.md). Recheck on module/platform changes. Unused browser/network features, codecs and optional formats are disabled; do not silently add ASIO or another SDK with separate terms.
- New dependencies require a pin, purpose, checked license, runtime/build-only classification and redistribution notes here. Python analysis packages are not required or selected yet; initial tests use CTest with an in-repository executable, avoiding another test-framework dependency.

M4.3u2 Linux native GUI tests directly link the already-required Ubuntu 24.04 libX11 (MIT/X11, no-cost system library; exact distro version in ci-environment.txt). This test-only fixture creates window-protocol atoms for bare Xvfb; no new acquisition, runtime library, bundled component or vendor patch. Existing [system-library license/notices register](ci.md) applies.

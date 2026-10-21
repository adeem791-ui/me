# KytyPS5 — experimental Windows stack-overflow build

This repository contains **an experimental patch and GitHub Actions build recipe**, not PS5 game files or a compiled emulator.

The Windows workflow downloads [KytyPS5](https://github.com/KytyPS5/KytyPS5) at pinned commit `7b7f9a7`, fetches its submodules, applies an experimental iterative traversal fix to `CollectScalarRead()`, runs shader tests, and packages a Windows launcher with its dependencies.

## Building the portable Windows version
1. Open the **Actions** tab in this repository.
2. Select **Build portable KytyPS5 (Windows)**, then **Run workflow**.
3. Wait for the build and tests to finish successfully.
4. Download the **KytyPS5-Portable-Windows-StackFix** artifact.
5. Unzip it, then unzip the enclosed portable ZIP if present, and launch `kyty_launcher.exe`.

**No Visual Studio or other build tools are needed on the PC that runs the completed package**. Windows and up-to-date Vulkan-capable GPU drivers are still required.

## Limitations and safety
- **Unverified:** The build has not yet compiled successfully on GitHub Actions and the crash fix has not been confirmed to make *Ghost of Yōtei* playable.
- A stack-overflow exception `0xC00000FD` was observed in this game, but resolving that exception may reveal other emulation issues.
- Test in a new folder; back up your old emulator folder before experimenting.
- Do not upload games, console firmware, personal logs, crash dumps, or proprietary game content to this **public** repository.
- This is an independent, unofficial build, not an official KytyPS5 release.

## Upstream and license
Original emulator: [KytyPS5/KytyPS5](https://github.com/KytyPS5/KytyPS5).

Upstream source is GPL-2.0 licensed; the distributed executable should retain the original license and provide access to corresponding source code and the modification. The patches are in `patches/` and are applied in numeric order by the workflow. `patches/06-driver-cache-file-tests.patch` adds GPU-free tests for the Vulkan pipeline cache file and clearer cache-invalidation logging; see `docs/pipeline-cache-file-tests.md`.

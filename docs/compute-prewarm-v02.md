# KytyPS5 compute-pipeline recorder & prewarmer — EXPERIMENTAL v0.2

This is **actual source code** for a compute-only pipeline capture/warmup system; it is **not a finished Ghost of Yōtei offline all-shader precompiler**. It cannot promise crash-free gameplay or fix VRAM exhaustion, and it does not precompile vertex/pixel shaders. Do not expect it to fix all stutters.

## What's built

- `patches/06-record-known-compute-pipelines.patch`: when opted in, Kyty writes `.kcs` recipes for **successfully compiled compute pipelines** (SPIR-V, descriptor layouts, push constants and required subgroup sizes).
- `tools/kyty_compute_prewarm.cpp`: separate Windows Vulkan tool that reads `.kcs` records, compiles known pipelines with up to 4 CPU worker threads, merges driver-cache contents and writes the existing `KytyPC2` checksum-signed cache file. No GPU dispatches are executed.
- `RECORD_COMPUTE_SHADERS.cmd`: starts Kyty and captures known compute shaders.
- `PRECOMPILE_KNOWN_COMPUTE.cmd`: runs the prewarmer while the emulator is closed.

## Get a Windows executable

This package contains **source code**, not a Windows `.exe`. To obtain binaries, open the GitHub feature branch's `Build portable KytyPS5 (Windows)` workflow in Actions. Download its Windows artifact **only if the build has a green check**. The build packages `kyty_compute_prewarm.exe` and both `.cmd` scripts beside the emulator. If no artifact is available or the workflow is red, the tool has not been verified to compile.

## Use (after successful Windows build)

1. Extract the experimental portable ZIP to a new folder.
2. Double-click `RECORD_COMPUTE_SHADERS.cmd`. Play the game long enough to encounter a few compute pipelines. This first session **still compiles normally** and may be slow or crash because of the known VRAM problem. The recording switch is off during normal launches.
3. Close the emulator completely, then run `PRECOMPILE_KNOWN_COMPUTE.cmd` and follow its prompts.
4. Launch via `START_SHARED_SHADER_CACHE.cmd` to reuse the driver cache. Check the new log for `Vulkan pipeline cache: loaded X MB`.
5. This prepares **previously recorded** compute pipelines only. A truly cold-run offline precompiler needs a shader-index parser and pipeline-state reconstruction, which are not implemented here.

## Validation

`kyty_compute_prewarm.exe --selftest` runs parser checks without a game or GPU. CI also validates application of the recorder patch after the previous five patches and builds the tools. **The user's NVIDIA RTX 5080 runtime remains untested** until they test the resulting Windows artifact.

## Boundaries and safety

- Captured `.kcs` and `.spv` records contain game-derived shader instructions and remain **on the user's PC**, under `%LOCALAPPDATA%\KytyPS5\PipelineCache\compute_recordings\PPSA26344`. Never upload them to public GitHub.
- Recorded pipelines have been translated and compiled by the existing emulator; this tool does not infer missing runtime resource specializations from the KCAP `all_shaders.xpps` archive.
- A driver cache is not a portable shader binary. It may be invalidated by GPU, driver, or content changes; cache hits are not guaranteed by Vulkan. The existing data checksum/driver UUID validation still applies.
- **Never prewarm while the emulator is running**; both may modify the same cache file. The script explicitly asks you to close the emulator.
- Runs without executing game assets and uses no network access. No game files are modified.
- The prior VRAM leak and rendering synchronization issues are separate; this prototype does not solve them.
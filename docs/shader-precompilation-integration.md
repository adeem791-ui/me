# Shader precompilation integration study — Ghost of Yōtei

**Status:** source review and integration design only; **no shader precompiler has been ported or validated yet**.

## Sources compared

- User-supplied KytyPS5 October 13 source ZIP (`KytyPS5-20261013`): a Demon’s Souls-specific static precompilation fork.
- KytyPS5 October 20 source at `7b7f9a7`, currently the base of this repo's experimental Windows build.
- Latest October 20 *Ghost of Yōtei* runtime log from a source build `7b7f9a7-dirty`, game `PPSA26344`, app version `01.008.000`. **The raw log and proprietary game assets are not included in this public repository.**

## What the reference fork actually implements

1. `src/local/static-seeds.cpp` and `tools/local/static-precompile/`: discover shader code, AGC header metadata, and VS/PS pipeline pairings from the Demon’s Souls game files. In particular, the scanner looks for `CSDR` bundles, `.cmat` material files, and `pass-states.json` render-state rules. **It is game-specific** and even hard-codes `KytyShaderSeeds2:PPSA01341\n` as a seed identity; this parser cannot be assumed to discover Ghost of Yōtei shaders.
2. `src/local/shader-precompile.cpp`: a standalone `kyty_shader_precompile` executable, operating on seed files. `precompile-windows.ps1` orchestrates sharded compilation to avoid a single large driver compilation process, caching by game version and GPU/driver.
3. `src/local/shader-warmup-cache.h`: serializes compiler **inputs**, resource specialization and pipeline recipes, not raw live GPU resource snapshots.
4. `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`: records encountered permutations; replays them during warmup; enables background SPIR-V translation/prefetch; integrates a static Vulkan cache and optionally per-pipeline blobs or binaries. The reference uses `KYTY_SHADER_WARMUP`, `KYTY_SHADER_PREFETCH`, and `KYTY_STATIC_PIPELINE_CACHE` controls.
5. `src/local/shader-hints.cpp`: captures resource specialization hints without distributing shaders compiled from copyrighted game code.
6. The reference README reports Demon’s Souls-specific coverage and stutter experiments. **Those measurements cannot be carried over to Ghost of Yōtei.**

## Observations from the current Ghost of Yōtei log

- The game now reaches executable/game startup past the previous logo crash.
- The Vulkan pipeline cache was initially empty and was later saved at increasing sizes, ending at **101.50 MB**.
- **594** compute-shader IR/SPIR-V phase-completion sequences are present.
- **594** `vkCreateComputePipelines begin` records and **593** matching `done` records appear. The unmatched last `begin` does not by itself establish a crash; a log can be interrupted.
- Reported CPU shader stages total roughly **2,169 ms of IR** and **1,288 ms of SPIR-V generation** across those sequences. These stage totals **exclude Vulkan driver compilation**, pipeline synchronization and other time on the game's critical path.
- The log is around 54 MB with over 1.3 million lines. Detailed logging may itself hurt performance. Do not equate all slow gameplay with compilation without per-frame and driver timings.
- The currently inspected base code, `PipelineCache::InitializeDriverCache()`, only loads its file in Release builds; cache data includes GPU/driver identification. The next run must show `Vulkan pipeline cache: loaded ... MB` to establish reuse.

## Why this is NOT a copy-and-paste port

The October 13 fork's `pipelineCache.cpp` has approximately 2,698 lines versus approximately 1,074 lines in the October 20 base, with significant differences in IR specialization and shader program interfaces. Its `ResourceTracking.cpp` also diverges sharply. Importing one file wholesale would lose newer emulator and stack-overflow changes and may not compile.

## Recommended staged implementation

**Stage 0 — validate reuse and instrument hotspots**
- Reboot the same portable install and confirm `_PipelineCache/PPSA26344.bin` is loaded, rather than silently starting empty. Preserve the folder, driver and game title.
- Use silent shader logging for play; collect only aggregated IR, SPIR-V, Vulkan-driver pipeline creation and frame-stall timings for controlled tests.
- Keep the proven *symptom* fix (iterative resource traversal) but verify it is semantically correct against deep and cyclic SSA shader tests.

**Stage 1 — recording-based warmup (portable, not game-format specific)**
- Port a minimal, bounded, versioned serialization layer adapted to the **current** `ProgramCache::Get()` inputs: stage, shader identity/hash, code, static state, function code where necessary, user-data shape, resource specialization and push-data cursor. Do not serialize raw runtime pointers or proprietary shaders to this public repo.
- Record actual compilations and graphics/compute pipeline recipes as the game runs. Save locally, safely and atomically on graceful exit and periodic checkpoints.
- On subsequent launch, explicitly pretranslate/replay **previously seen** shaders before the game starts; cache Vulkan pipelines when compatible, and retain local compiled input records so a new version can invalidate stale entries.
- Use a dedicated low-priority, cancellable worker pool. Resource-state revalidation is mandatory at first use; a warmup hit must be an exact specialization match.
- Add progress, timeout and a fallback to normal lazy compilation if any record is corrupt, stale, or fails to compile.

**Stage 2 — optional static offline discovery**
- Investigate Ghost of Yōtei's installed data layout locally, without publishing game content. Determine whether there are readable shader bundles plus reliable shader stage, entry point, resource specialization and graphics pipeline state metadata.
- Only then adapt `static-seeds.cpp`/material scanner (currently Demon’s Souls-specific). Mark incomplete coverage and do not claim the static seed generator can find every shader.
- For Vulkan static artifacts, key by the driver's UUID/version and invalidate correctly on driver changes. Avoid multi-GB cache allocations without limits.

**Stage 3 — evaluate background optimizations**
- Consider driver-supported pipeline binaries, pipeline fail-on-compile-required and first-use unoptimized pipelines only after confirming hardware support and safe integration with the current Vulkan backend.
- Limit CPU/VRAM budgets, bound compilation queues and compare same scenes and same driver before claiming a win.

## Definition of success

1. Standard Windows GitHub Actions build and existing regression tests pass.
2. First run still reaches the Ghost of Yōtei main screen without regression or a stack overflow.
3. First run records a local shader/pipeline-input cache.
4. Second launch visibly loads it and prepares known permutations before entering gameplay.
5. Replaying identical scenes reduces shader/pipeline creation stalls; new unseen scenes may still compile on demand.
6. Cache invalidation survives game update, driver update, emulator update and malformed/incomplete files.

**Do not upload** game files, compiled game shaders, user crash dumps, or system paths containing personal information to this public repository. A generic replay cache written on the user’s PC is different from distributing precompiled game content.

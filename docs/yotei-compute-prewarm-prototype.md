# KytyPS5: Compute Pipeline Capture & Prewarm — experimental v0.1

**This is runnable source for the first compute-only stage, NOT a finished all-shaders precompiler.** It is not guaranteed crash-free, nor does it solve VRAM exhaustion or graphics-pipeline stutters. The precompiler can only prepare **compute pipelines that the emulator has already encountered and recorded**. Ghost of Yōtei `all_shaders.xpps` still requires program-record and resource-specialization reverse engineering for cold-start full coverage. We have NOT verified the emulator runtime on the user's Windows PC.

## What is included

- `patches/06-record-known-compute-pipelines.patch` adds **opt-in** per-pipeline capture at the point where Kyty successfully compiles real Vulkan compute pipelines. Stores SPIR-V instructions plus the exact relevant compute pipeline recipe in `.kcs` files. No game archives are uploaded.
- `tools/kyty_compute_prewarm.cpp` is a standalone native Win64 Vulkan precompiler. It reads `.kcs` files, creates matching compute pipelines with up to 2–4 worker threads, and writes a **KytyPC2 compatible checksum/driver-UUID validated Vulkan cache** into the shared path.
- `RECORD_COMPUTE_SHADERS.cmd` opts into recording when running the game.
- `PRECOMPILE_KNOWN_COMPUTE.cmd` prewarms the known compute pipelines. Requires the rebuilt `kyty_compute_prewarm.exe` beside the emulator launcher.

## Running it (after GitHub Actions succeeds)

1. Extract the **new experimental build** to a fresh folder. Do not replace a currently running executable.
2. Run `RECORD_COMPUTE_SHADERS.cmd` once while you play the game far enough to encounter shaders. This records only pipelines that already compile successfully. It may impose some recording-time slowdown.
3. Close the emulator completely.
4. Run `PRECOMPILE_KNOWN_COMPUTE.cmd`. Let it finish. It will use the NVIDIA GPU for **compilation via the driver**, not for parallel shader translation; the CPU workers submit distinct compute pipeline creations.
5. Run `START_SHARED_SHADER_CACHE.cmd` for the warm run and inspect the log for `Vulkan pipeline cache: loaded ... MB`.

**WARNING:** The old VRAM leak can still crash Ghost of Yōtei. This tool does not repair that leak. Precompile only while the emulator is closed to avoid races on the same cache file. **Do not share files in `compute_recordings/` publicly**, as they contain original shader-derived machine instructions.

## Build status and acceptance criteria

- The patch `git apply --check` has been verified against the pinned original Kyty source `7b7f9a7`.
- Code has NOT been executed with the Vulkan driver / RTX 5080 in this environment. Check GitHub Actions for a successful Windows compile before trying the tool.
- Verify that a cold launch records `.kcs` entries; the standalone precompiler prints completed/failed counts; the next emulator run loads and uses a compatible Vulkan cache; and replayed pipeline timings actually improve over uncached runs.
- Compute records capture shader IR translated to SPIR-V, not undiscovered full game shader archives, nor vertex/pixel shaders. The multithreaded replay is experimental, bounded to a maximum four workers, and does not force PS5 guest threads onto CPU cores.
- The warmup process intentionally never invokes shader dispatches and cannot increase gameplay FPS where render synchronization or VRAM exhaustion dominates.

## Safeguards

Files are bounded and validated before the tool processes them. It refuses GPUs without NVIDIA vendor ID to avoid accidentally writing caches for another GPU, and stores the existing Kyty hash and GPU/driver signature. The Vulkan cache is saved through a temporary file followed by an atomic replacement. No game files are touched.
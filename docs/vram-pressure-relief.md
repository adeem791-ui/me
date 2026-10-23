# VRAM pressure relief for the Ghost of Yōtei out-of-memory crash (patch 17)

**Status:** source change plus a GPU-free unit test. It compiles in the Windows CI build. **Not tested in the game.** Don't treat the crash as fixed until Ahmad has run the manual test below.

Patch: `patches/17-vram-pressure-relief.patch`. It applies on upstream `9dcff95` after `patches/01-16-rebased-on-9dcff95.patch`.

## The crash
```
vmaCreateImage failed: result=ErrorOutOfDeviceMemory extent=4096x4096x1 format=141 layers=1 levels=13
heap usage 16,463,478,784   budget 15,160,971,264
VMA allocated 15,098,714,176   VMA blocks 15,399,548,928
```
`Image::Image` then calls `EXIT`, so the emulator closes.

## Diagnosis

### Facts, checked in source or computed from the log
1. **The failing image is small.** Format 141 is `BC5_UNORM`. A 4096² image with 13 mips is about 22 MB, so it is only the last allocation, not the cause.
2. **This is not fragmentation.** VMA blocks minus VMA allocations is about 300 MB, roughly 2%.
3. **About 1.06 GB of device memory is outside VMA.** The heap shows 16.46 GB used, but VMA blocks hold only 15.40 GB. Every emulator allocation goes through VMA, so the emulator can't free this part.
4. **Images are allocated without a budget check.** `vma.cpp` `CreateImage` uses `DEVICE_LOCAL` and does not set `VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT`. The first warning is the failure itself.
5. **Memory is freed only between submissions:**
   - The texture-cache collector (`RunGarbageCollector`) runs only at submission boundaries (`graphicsRun.cpp` `GuestGpu::Process`) and at suspend points.
   - Destroyed images are freed through `CommandScheduler::DeferOperation`, only once the GPU passes the tick they were last used on.
   - So one long submission, such as a large texture-streaming burst, can keep allocating with no chance to collect or free.
6. **The cache's size accounting is wrong in two ways:**
   - `Image::AccountedSize` counted the guest data size, not the real host allocation size, which is often larger because of alignment and tiling.
   - The cache subtracts an image's size when it is unregistered, but the VRAM is freed later, after the GPU wait.
7. **PlayGo is not involved:**
   - The loader tries `playgo-chunk.dat` → `playgo-chunkdefs.xml` → `Data/chunkmanifest`, logging "Can't open" for each missing file.
   - It logs `PlayGo: chunks num = N` once one loads.
   - `GetLocus` reports every chunk as `LOCAL_FAST`.
   - None of this touches GPU memory.

### Hypotheses, not verified
- The ~1.06 GB outside VMA is driver memory: swapchain, pipeline binaries and descriptor pools.
- The monitor CSV's drop to about 1.6 GB is the process exiting, not a recovery.
- The black screen is a separate problem, caused by long single-thread `vkCreateComputePipelines` calls (up to 27.6 s, 135.7 s in 5 minutes in the earlier log). The heartbeat line below separates the two.

## Source map (upstream 9dcff95 + p16)
| What | Where |
|---|---|
| Image allocation and the failure log | `src/graphics/host_gpu/vma.cpp` `CreateImage` |
| EXIT on failure | `src/graphics/host_gpu/renderer/image/image.cpp` `Image::Image` |
| Size used for cache accounting | `renderer/image/image.h` `AccountedSize` |
| Texture-cache collector and LRU | `renderer/cache/textureCache.cpp` `RunGarbageCollector`, `FreeImage`, `DeleteImage` |
| Deferred destroy | `renderer/commandScheduler.cpp` `DeferOperation` / `PopPendingOperations` |
| Collector call site (between submissions only) | `src/graphics/guest_gpu/graphicsRun.cpp` `GuestGpu::Process` |
| Draw / dispatch start | `renderer/renderDraw.cpp` `DrawIndex`, `DrawAuto`; `renderer/renderCompute.cpp` `DispatchDirect`, `DispatchIndirect` |
| PlayGo | `src/libs/libPlayGo.cpp` (unrelated to the crash) |

## What patch 17 changes
1. **Relief at draw/dispatch start** (`RenderContext::RelieveMemoryPressure`). It runs right after `PopPendingOperations()`:
   - It measures **committed** device memory: heap usage minus the free space inside VMA blocks.
   - It does nothing while that is below the trigger, `budget − headroom`, with headroom 1 GiB by default.
   - At or above the trigger, it evicts least-recently-used texture-cache images down to `budget − 2×headroom`.
   - It then runs `CommandScheduler::Finish()` once, so the deferred destroys actually free memory before this draw allocates.
   - Upstream already restarts the scheduler at this point for stream-buffer wraps and buffer downloads ("Resource preparation above may synchronously finish and restart the scheduler").
2. **Eviction is conservative** (`TextureCache::ReclaimUnusedImages`). It never touches an image that is:
   - used since the last collector pass, which includes everything in the open command buffer;
   - GPU-modified, meaning its contents exist only on the GPU;
   - referenced by a resident image heap;
   - a depth image with a GPU-modified stencil.

   Images it does evict are re-uploaded from guest memory the next time they're used, the same as after the normal collector.
3. **No GPU wait on every draw when nothing helps.** If relief can't get under the trigger, the next attempt waits until usage grows another 256 MiB. The policy is in `vramRelief.h` and covered by `tests/VramReliefTests.cpp`.
4. **Correct accounting.** `AccountedSize` now uses the larger of the guest size and the real VMA allocation size.
5. **Better diagnostics, all bounded:**
   - The `vmaCreateImage failed` log now adds the format name, samples, tiling, usage, flags and the driver's required bytes, then the per-heap budget.
   - The EXIT message names the format.
   - `Device memory stats` (every 5 s) adds `committed=` and the relief counters.
   - **`Heartbeat(5s)`** logs every 5 s, always, with:
     - `local_time` (your PC's clock, to the millisecond), `uptime_s`, and `since_last_ms`, the gap since the previous heartbeat;
     - `gpu_ticks_behind`, the GPU work submitted but not yet finished.
     - With **"Show performance panel"** ticked in the launcher's configuration, or F2 pressed in game, it also logs submits, draws, dispatches, flips, dropped presents, CPU waits on the GPU, pipelines created and their compile time, texture evictions, audio underruns and file reads.
   - **The heartbeat stops if the loop it runs in stalls.** It is logged from the GPU submission loop, so if that loop is blocked (for example a long pipeline compile, or a GPU wait that never returns), the heartbeat lines stop.
     - The **last heartbeat's `local_time`** is the last moment the loop was confirmed alive.
     - The log can't tell you anything after that point. For that case, use **your external CPU/GPU monitor data and a screen capture**, lined up on that `local_time`.
     - A `since_last_ms` much larger than 5000 on a later line means the loop stalled for that long and then recovered.
6. **Switches.** Both are read once at startup and logged as `VRAM relief: enabled, headroom=… MiB`.
   - `KYTY_VRAM_RECOVERY=0` turns relief off.
   - `KYTY_VRAM_HEADROOM_MB` (256–4096, default 1024) sets the headroom.

### Not changed
- No game-specific profile.
- No shader, draw or dispatch is skipped.
- No resolution or quality reduction.
- No CPU affinity or scheduling change.
- Below the trigger, the only per-draw cost is one VMA budget read.

## Manual test (Ahmad)
Use the CI zip `KytyPS5-Yotei-VRAM-Relief-Experimental`, unzipped into a **new folder**. Don't overwrite your current emulator.
1. In the launcher, open the Ghost of Yōtei configuration and tick **Show performance panel**.
2. Run the same route that crashed before, for at least as long as it took to crash.
3. Note whether it crashes, the time to crash, any visual problems (missing or blurry textures, flicker), and stutter.
4. Run your CPU/GPU monitor script and a screen capture (for example the Xbox Game Bar recorder) for the whole run, with your PC clock visible or noted at the start. If the screen goes black, these are the only record after the last heartbeat.
5. **A/B check:** run once with `START_WITHOUT_VRAM_RELIEF.cmd`, which sets `KYTY_VRAM_RECOVERY=0`. That should behave like the old build.
6. Send `_kyty.txt` for both runs, plus the monitor CSVs and a screenshot or short clip of anything odd.

### Log lines to look for
- `VRAM relief: enabled, headroom=1024 MiB` confirms the patch is active.
- `VRAM relief #N: committed_before=… evicted_images=… gpu_wait=1 committed_after=… released=…MiB took=…ms` means relief worked. Large `took` values mean visible hitches.
- `(still above trigger: nothing more is safely evictable now)` means the cache had nothing left that was safe to drop. If a crash follows, the memory is somewhere this patch can't touch.
- `vmaCreateImage failed: … format=… required_bytes=…` followed by the heap budget means it still ran out of memory. Send that log.
- `Heartbeat(5s): local_time=…` every ~5 s. If they stop while the window is still open, note the last `local_time` and check the monitor CSV and recording from that time on.

## Rollback
- **For a single run:** launch with `START_WITHOUT_VRAM_RELIEF.cmd`.
- **Fully:** delete the new folder and keep using your old one. Nothing outside the new folder is changed.
- **In source:** `git apply -R patches/17-vram-pressure-relief.patch`.

## Limitations
- **Not run on a GPU.** The emulator compiles in Windows CI, but only the policy test has run: GCC with ASan/UBSan, and Clang. A deliberately wrong threshold makes it fail.
- **The heartbeat can't report a stall of its own loop.** It goes silent instead; external monitoring and a screen capture cover that case.
- **Relief can't free memory outside VMA** (the ~1.06 GB), or images that are in use or GPU-modified. If most of the 15 GB is in those, the crash can still happen. The new log lines will show that case.
- **Each relief stalls the CPU until the GPU finishes**, so frequent relief can cause stutter. The per-attempt time is logged.
- **Evicted textures are uploaded again** when used again, which costs bandwidth but not correctness.
- **Headroom default is a guess.** 1 GiB was chosen from this log, not measured. It may need tuning with `KYTY_VRAM_HEADROOM_MB`.

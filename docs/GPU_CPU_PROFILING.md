# Kyty GPU/CPU bottleneck profiler — experimental

This is a **measurement feature**, not an FPS improvement claim. It does not force CPU affinity, change shader scheduling or synchronize GPU work differently. The in-emulator profiler is **off by default**.

## After a successful Windows build
1. Extract the new portable artifact into a separate folder.
2. Open START_GPU_CPU_PROFILE.cmd and launch Ghost of Yotei. Remain in a slow (5–6 FPS) scene for at least 20 seconds.
3. While the game is open, double-click PROFILE_CPU_THREADS.cmd. It captures around 60 seconds of per-thread CPU timing and writes Kyty_CPU_Thread_Profile_*.csv next to the scripts.
4. The emulator's usual _kyty log contains 'GpuCpuProfile:' lines every ~5 seconds. Share those lines and the CPU CSV if you want an evidence-based speed optimization. No game archives, shader recordings or files containing private paths need be uploaded.

## Interpretation
* busy_ms is CPU-side elapsed wall time processing GPU queue submissions/commands, **including Vulkan driver compilation and CPU synchronization**. It is NOT measured GPU execution time.
* graphics_ms, compute_ms, control_ms, command_ms separate parts of busy_ms.
* idle_wait_ms = no guest GPU commands waiting in Kyty's queue; investigate command producer/guest CPU.
* blocked_wait_ms, blocked_polls, incomplete_slices = serialized GPU queue work waiting for guest-side dependencies. High values suggest synchronization work rather than simply more CPU threads.
* peak_queue = max observed pending submissions.
* ProcessCpuPercentOneCore100 is summed CPU use where **100% is one logical CPU**, so it may exceed 100. BusiestThreadPercent reveals whether a single CPU thread is saturated.

## Confirmed source details
* In upstream Kyty commit 7b7f9a7, GuestGpu::ThreadRun is created on a **single std::jthread**, serializing guest GPU queue command processing. More guest GPU threads without ordering guarantees could corrupt rendering.
* CondVar::WaitFor(mutex, 100) accepts **microseconds**; Windows rounds to about 1 millisecond. It is not a 100 ms pause.
* Prior game logs show 795 Vulkan compute pipeline compilations totaling about 230 seconds and VRAM out-of-memory. A successful shader warmup may reduce initial stalls but does not fix other runtime bottlenecks.

Profiling should inform future, carefully bounded parallelization of independent shader translation/compilation tasks. No performance gain has been demonstrated yet.

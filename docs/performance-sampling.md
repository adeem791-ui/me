# CPU/GPU performance sampling — experimental Windows build

This standalone logger is deliberately **diagnostic**, not a CPU-affinity/priority hack or a claim of a performance fix. It uses Windows PowerShell 5.1 built into Windows; `nvidia-smi` is optional.

## One-click use (portable artifact)

1. Download and extract a **new** portable Windows artifact built after this change is merged. Keep the files together.
2. Double-click **`START_WITH_TELEMETRY.cmd`**. It launches KytyPS5 (using the persistent driver pipeline cache launcher when available) and starts a 15-minute local sampler. You may need to choose/start the game through the launcher during this period.
3. Play or reproduce the shader compile stall. For an A/B experiment, run the same scene and duration on a cold cache and then a warm cache, using the same driver and build. Do not clear the cache between runs you intend to compare as warm.
4. Find the timestamped CSV under `%LOCALAPPDATA%\KytyPS5\Telemetry\performance-YYYYMMDD-HHMMSS.csv`. The script prints the exact path. Kyty's internal `_kyty.txt` / `PipelineTrace` lines, if produced, remain separate; match their timestamps to the sample timeline.

You can run `SAMPLE_EMULATOR_PERFORMANCE.ps1` on an *already running* emulator instead of launching another copy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\SAMPLE_EMULATOR_PERFORMANCE.ps1 -IntervalSeconds 2 -DurationSeconds 180 -ProcessName kyty_emulator
```

The one-click launcher uses the installed KytyPS5 executable. The `.ps1` script also works outside the portable folder on a machine with the emulator running.

## Fields and interpretation

| Column | Meaning |
|---|---|
| `cpu_core_equivalents` | Process CPU seconds consumed per wall second. ~1.0 means one logical core's worth of work; can be above 1.0 on multiple threads. |
| `cpu_percent_whole_machine` | Process CPU relative to all logical processors (24 logical processors yields ~4.17% for 1 core busy). |
| `busy_threads_top3_pct_one_core` | Three busiest sampled OS threads; values are **per logical core**, not whole-machine percentages. E.g. `1234:99.0%` suggests a nearly saturated thread. |
| `thread_count` | Process's observed OS thread count, not number of simultaneously busy cores. |
| `ram_working_set_mib` / `private_memory_mib` | Windows process memory counters. |
| `gpu_utilization_whole_pct` | **Whole GPU** utilization from NVIDIA's `nvidia-smi`; could include other applications, and is not per-emulator usage. |
| `vram_used_whole_mib` / `vram_total_whole_mib` | Whole-GPU VRAM usage and total capacity reported by `nvidia-smi`; not Vulkan's per-heap budget. |

First CPU sample for a PID is blank until a second sample supplies a time delta. Counters may be blank when the target process or NVIDIA utility is unavailable. If there are multiple NVIDIA GPUs, the script prefers a GPU whose name includes `5080`; otherwise it uses the first. No attempt is made to change CPU affinity, power plans, GPU scheduling, Vulkan synchronization, or priority.

## What would indicate the bottleneck?

- **One hot emulator thread + near-zero core equivalents from other threads + low whole-GPU usage during `PipelineTrace` stalls:** likely a serial CPU-side render/driver bottleneck.
- **Many CPU cores busy + low whole-GPU usage:** may be shader compilation, emulation overhead, synchronization, or GPU driver work; measure time spent in the call paths before changing scheduling.
- **High VRAM usage or repeated OOM while pipeline compilation is otherwise fast:** separately investigate resource lifetime, eviction, allocations and Vulkan budget.
- **Warm vs cold cache:** look for cache-acceptance lines and compare pipeline creation duration distributions; merely finding a cache file does not prove that all needed pipelines were cached.

An external sampler **cannot** measure emulator frame times or per-pipeline durations by itself; those require in-emulator instrumentation. Internal logs and this CSV should be compared with aligned time stamps, not assumed identical.

## Safety and limitations

The sampler never uploads data. Generated logs are local and may still include identifiers such as machine process IDs and file paths; review/sanitize before sharing. Never post game files, shader archives, firmware, sensitive logs, or crash dumps to a public repository.

Sampling consumes a little CPU and `nvidia-smi` can take noticeable time; a 2-second interval is the default. The initial version has **not been exercised on the user's Windows machine**. Test its output on that PC before drawing conclusions. A successful workflow build doesn't prove game compatibility or FPS improvements.

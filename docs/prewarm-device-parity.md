# Prewarmer device parity and cache-hit logging — EXPERIMENTAL

**Status:** source change plus GPU-free tests only. **Whether it raises cache hits on the RTX 5080 is not known yet.** That needs the test below on the user's PC.

## Why
`kyty_compute_prewarm` writes a cache file the emulator loads, since both use the same `KytyPC2` format. But the prewarmer used to create its Vulkan device with every core 1.0/1.2/1.3 feature the GPU offers, plus only `VK_KHR_push_descriptor`. The emulator instead enables a specific set, including `VK_EXT_robustness2` (`robustBufferAccess2`, `robustImageAccess2`, `nullDescriptor`).

Robustness changes how out-of-bounds accesses are compiled. A driver may therefore treat the prewarmed pipelines as different from the emulator's, so they **miss** even though the file loads. This is a hypothesis; nothing has proven a miss yet.

## What changed
- `tools/prewarm_device_profile.h`: builds the prewarmer's device features and extensions from the emulator's `VulkanCreateDevice()` (upstream `vulkanWindow.cpp` at `7b7f9a7`). It covers the core feature list, the required 1.1/1.2/1.3 features, and the optional bits the emulator enables when supported: `shaderSharedInt64Atomics`, `robustImageAccess`, `subgroupSizeControl` (same wave64 rule), and `shaderFloat64` (same float-controls rule). It also enables `VK_EXT_robustness2`, `VK_KHR_workgroup_memory_explicit_layout`, `VK_EXT_shader_image_atomic_int64` and `VK_KHR_maintenance8` when present. It **never enables a feature or extension the GPU lacks.** Graphics-only state (swapchain, depth clip, color write, mesh, barycentrics, feedback loops, provoking vertex) is left out.
- The prewarmer prints one line, `Device profile: robustBufferAccess=… robustBufferAccess2=… robustImageAccess=… robustImageAccess2=… nullDescriptor=… subgroupSizeControl=… extensions=…`. Compare it with the emulator's `Vulkan robustness:` and `Vulkan subgroup:` log lines.
- `patches/08-compute-pipeline-cache-feedback.patch`: the emulator's `vkCreateComputePipelines done` log line now ends with `cache_feedback=hit|no_hit_reported|unavailable driver_ms=N`, from Vulkan 1.3 `VkPipelineCreationFeedback`. It only reports; pipeline creation is unchanged.
  - `hit`: the driver set `VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT`, meaning it reports that the supplied cache let it avoid most of the creation work. This is the useful positive signal.
  - `no_hit_reported`: the bit was clear. This is **not proof of a miss**: the driver may still have reused part of the cache without setting the bit. Read it together with `elapsed_ms`.
  - `unavailable`: feedback was turned off or the driver did not mark it valid. `driver_ms` is then `-1`.
  - Set `KYTY_PIPELINE_FEEDBACK=0` to turn it off.

**Same GPU:** the prewarmer picks the NVIDIA discrete GPU. If the emulator uses a different GPU or driver, the cache signature differs, and with #2 the emulator logs `invalidating … (GPU or driver changed)`.

## Tests
- `kyty_compute_prewarm.exe --selftest` (Windows CI) now also prints `Device profile self-test: PASS`. It checks:
  - robustness2 is enabled and in the `vkCreateDevice` chain when available;
  - nothing is enabled beyond what the device reports, for both a full and a limited fake device;
  - an absent extension is never chained;
  - the `shaderFloat64` and wave64 rules behave like the emulator's.
- The same checks run on Linux without a GPU: `g++ -std=c++17 -I<Vulkan-Headers>/include -Itools tools/prewarm_device_profile_test.cpp && ./a.out`.

## Test plan on the user's PC (RTX 5080)
1. Record as usual (`RECORD_COMPUTE_SHADERS.cmd`), then close the emulator.
2. **A, old prewarmer:** move the cache file aside. Prewarm with the previous build, launch the game, play the same opening 2–3 minutes, and quit.
3. **B, this build:** move the cache file aside again. Prewarm with this build and repeat the same play.
4. **C, emulator-warmed reference:** launch a second time with the cache the emulator saved itself.
5. For each run, collect the `vkCreateComputePipelines done … shader=… elapsed_ms=… cache_feedback=…` lines (no paths or game data). Compare by shader hash: share of `cache_feedback=hit`, plus total and worst `elapsed_ms`.

Expected if the hypothesis is right: B and C show mostly `cache_feedback=hit` with much lower `elapsed_ms`, while A shows few hits and higher `elapsed_ms`. If A already shows mostly `cache_feedback=hit` and low times, the hypothesis is wrong, and this change only removes a risk. Base conclusions on `hit` counts and `elapsed_ms`. A high `no_hit_reported` count alone does not show that the cache was ignored.

## Limits
- A populated `VkPipelineCache` never guarantees hits. Drivers may also key on state not mirrored here.
- This is compute only. It does not fix graphics pipeline stalls, VRAM exhaustion, or single-thread GPU command processing.

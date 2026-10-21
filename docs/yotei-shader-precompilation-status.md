# Ghost of Yōtei shader-precompilation investigation (2026-10-20)

**Status: targeted runtime stability + persistent driver cache implemented as experimental patches. Full offline shader precompiler NOT yet implemented. Do not label the normal Windows build as a precompiling build.**

## Evidence: two October 20b ZIP uploads and Ghost of Yōtei logs

- `KytyPS5-v20261020b.zip` is a **Demon's Souls** portable release containing `kyty_shader_precompile.exe`, `precompile-windows.ps1`, `launcher.ps1`, and shipped `PPSA01341` hints.
- `KytyPS5-20261020b.zip` contains the corresponding source. Its `src/local/static-seeds.cpp` explicitly uses `KytyShaderSeeds2:PPSA01341` and discovers Demon’s Souls `CSDR`/AGC bundles and `.cmat` material pipeline pairs. Its UI's “Precompile shaders” button is not transferable without porting the shader/pipeline runtime and adapting source data discovery to Ghost of Yōtei.
- `_kyty(6).txt` and `_kyty(7).txt` are **byte-for-byte identical (SHA-256 `699637cc65a1c3c5a31e2ecaade9149ac594904add2807eab2b60594bdb9bf98`)**, not independent experiments.
- The single latest run completed **773/773 Vulkan compute pipeline creations**, taking approximately **205.45 seconds** in aggregate; worst case **46,476 ms**. The runtime still initialized the Vulkan pipeline cache empty.
- The latest fatal error is `!ShaderRecompiler::IR::MaterializeResources(resource_plan, runtime, resources, specialization)` in `pipelineCache.cpp:258`. There was **no** final `failed to create image` in these logs. Do not confuse this with the previous VRAM exhaustion failure.
- These logs expose user game paths and must not be uploaded to this public GitHub repository. Game assets and shader blobs are also excluded.

## Changes committed to the build

1. `patches/04-materialization-recovery.patch`: replace the current uncaught materialization assertion in shader program lookup with a throttled warning and a failed-stage return; skip graphics draws whose required shader stage is missing, as the Demon’s Souls fork already does in its more comprehensive handling. May skip effects/draws; needs live testing against `PPSA26344` and should be treated as experimental.
2. `patches/05-shared-driver-cache.patch`: support optional `KYTY_PIPELINE_CACHE_DIR` absolute path in the current pipeline cache. Existing driver signature/hash checks remain; no shared guest game files or shader dumps.
3. `scripts/START_SHARED_SHADER_CACHE.cmd`: one-click Windows launcher using `%LOCALAPPDATA%\\KytyPS5\\PipelineCache` across freshly extracted portable ZIPs. Copies any old per-title `_PipelineCache\\*.bin` once into the shared location if no shared copy exists. Run this CMD instead of the direct Qt launcher when you want persistent Vulkan caching.

The build workflow applies the stack fix, oversized compute compilation hint, VRAM-pressure mitigation, shader-materialization recovery, and optional shared-cache support; Windows and game runtime results must be checked on GitHub Actions and on the user's PC.

## Next engineering milestone — actual offline shader precompilation

A real Ghost of Yōtei precompiler needs at least **one** reliable source of shader compile inputs and graphics/compute pipeline recipes:

**A. Portable recording/replay first:** capture complete compilation input, shader code, stage static state, push-data shape, and *runtime-resolved* resource specialization into a versioned, checksummed local cache while the game executes. The standalone precompiler and renderer then share an ABI for recorded inputs, and the emulator loads compatible precompiled Vulkan pipelines on launch. Handle game updates, NVIDIA driver UUID/version changes, source-code updates, input revisions, and malformed cache data. Avoid serializing guest pointers or private dumps to the public repo.

**B. Static discovery later:** study Ghost of Yōtei's shader formats and pipeline recipes on the user's own PC. Do not assume the Demon’s Souls `CSDR` and `.cmat` scanner works for `PPSA26344`.

Porting the standalone `kyty_shader_precompile` executable without its full runtime integration will **not** accelerate the current `7b7f9a7` renderer. Displaying a “Precompile shaders” button without that backend would mislead users.

## Acceptance tests

1. Windows workflow compiles all patches and existing shader/regression tests pass.
2. Materialization errors cause a visible limited warning, not an immediate process fatal; verify failed graphics stages are not dereferenced.
3. Starting through `START_SHARED_SHADER_CACHE.cmd` loads compatible cache after the first run. The NVIDIA driver may still take time to recreate pipelines from cached data.
4. Log `initialized empty` on first boot, then `loaded X MB` on later launches. After a driver update, validation should reject stale data.
5. Only after the recording/replay backend is implemented should the launcher's precompile button be added.

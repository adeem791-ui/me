# Ghost of Yōtei large-compute-pipeline experiment

This is an **experimental workaround**, not a confirmed compatibility fix. The October 20 Ghost of Yōtei logs repeatedly ended graphical progress after `vkCreateComputePipelines begin` for shader `0xbe2f5f42f6b7c531` (more than 400,000 SPIR-V words), while audio continued.

The second patch in `patches/02-large-compute-fast-compile.patch` asks Vulkan to **disable expensive optimization for unusually large compute pipelines (at least 262,144 SPIR-V words)**. It retains shader behavior in principle, but the driver may ignore this hint or still take a long time. The patch also logs the shader hash, word count, chosen flag and actual pipeline creation duration.

## Testing the latest Windows artifact
1. Open the latest [GitHub Actions build](https://github.com/adeem791-ui/me/actions). Wait for a **green checkmark**. Download the artifact from the run that includes the latest compute-pipeline patch, **not** an older successful build.
2. Extract the portable emulator to a **new folder** and keep the old folder intact. Open `kyty_launcher.exe`. Use the same game files and start at 1280 × 720, HDR off, shader logging Silent.
3. Test for up to 2 minutes after the logo. Watch for new frames. If possible, send just the recent `PipelineTrace: vkCreateComputePipelines` lines from the local `_kyty.txt`.
4. If it still sticks after the logo, stop Kyty. Launch the optional `TEST_DIAGNOSTIC_SKIP_CS.cmd` from the new portable folder. It deliberately skips the single suspect compute shader. **This is only a debugging comparison**; it can break textures, post-processing, or game behavior. Do not use the bypass for normal gameplay.
5. If the diagnostic bypass still produces no frames, investigate video-output and thread synchronization instead of assuming one shader is the full cause.

## Developer settings
- `KYTY_LARGE_CS_FAST_COMPILE=0`: disable the new optimization hint and restore normal Vulkan compile flags.
- `KYTY_DIAGNOSTIC_SKIP_YOTEI_CS=1`: bypass the one known shader for **testing only**; disabled by default.

The patches were checked locally against the provided October 20 source. Windows compilation and game testing still have to be confirmed. This public repository contains **no game files, shader dumps, or crash dumps**.

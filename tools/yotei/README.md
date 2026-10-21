# Ghost of Yōtei shader-store inspector (research v0.1)

This **read-only Python inspector** validates the observed KCAP section layout in `cache_ps5/all_shaders.xpps`, and inspects the `all_shaders.texmeshman` companion manifest. It is **not a shader compiler**.

## Run on Windows

```powershell
py -3 inspect_yotei.py "D:\ps5 emulator\games\PPSA26344-app\cache_ps5\all_shaders.xpps" "D:\ps5 emulator\games\PPSA26344-app\cache_ps5\all_shaders.texmeshman"
```

It writes `Yotei_Shader_Inspection.json`. The report contains metadata and file hashes only, not game shader code or full paths. The two uploaded source files were already tested; no need to run this again unless the game version changes.

## Observed findings for PPSA26344 v01.008

- KCAP section table: 392 entries at `0xE0`, count field `0x68`.
- Largest section: 4,106,240 bytes starting at `0x27510`.
- A known GPU MIMG instruction window from [AnyPS5 issue #1941](https://github.com/boykopovar/AnyPS5/issues/1941) occurs four times in that section, including at `0xDCB6C`. This independently supports the presence of actual PS5 shader code.
- FNMS companion manifest: five resource names resembling `.sps` textures; not confirmed as shader index data.

**Limitations:** No verified shader program entry table, instruction boundaries, per-stage metadata, resource specialization or complete Vulkan pipeline recipe yet. The actual offline precompiler must reconstruct those safely rather than interpreting every KCAP section as one shader.

Do not commit proprietary game archives, executable shader bytes, or full personal log paths to this public repository.

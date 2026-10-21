# Vulkan pipeline cache: file-format tests and invalidation logging

`patches/06-driver-cache-file-tests.patch` (applied after patches 01–05).

## What it changes
- Moves the on-disk format of `_PipelineCache/<TITLE_ID>.bin` (signature, XXH3 hash, driver payload) out of `PipelineCache::InitializeDriverCache()` / `Save()` into a small header, `src/graphics/host_gpu/renderer/pipeline/driverCacheFile.h`, with no Vulkan or file I/O dependency.
- **The byte layout does not change.** Existing cache files still load. A test pins this.
- The load log now says *why* a cache was rejected, instead of one message ("driver or data mismatch") for all cases:

  | Log reason | Meaning | What to do |
  |---|---|---|
  | `GPU or driver changed` | vendor/device ID, driver version, or `pipelineCacheUUID` differs | Expected after a driver update; one slow run rebuilds it |
  | `payload checksum mismatch` | file truncated or corrupted (e.g. process killed mid-write on an old build) | Delete the `.bin` file |
  | `unknown file format` | not a Kyty cache file, or a different format version | Delete the `.bin` file |
  | `file too small` / `file too large` | empty, truncated header, or > 4 GB | Delete the `.bin` file |

- Logs the cache size and read+verify time, e.g. `Vulkan pipeline cache: read and verified 101.50 MB in 85.3 ms`.
- Logs when `KYTY_PIPELINE_CACHE_DIR` is set but ignored because it is not an absolute path (patch 05 ignored it silently).

## Tests
`tests/DriverCacheFileTests.cpp` → CMake target `driver_cache_file_tests`, CTest name `driver_cache_file`. No GPU, Vulkan loader or game data needed. It checks:
round trip (0 B – 1 MB payloads), legacy byte-layout compatibility, stable signature text, truncated header/payload, a flipped payload bit, driver-version / UUID / GPU changes, and foreign or old-format files.

Run where it runs:
- **Every PR touching `patches/` or workflows:** `.github/workflows/patch-tests.yml` (Linux, a few minutes) applies all six patches to `7b7f9a7` in workflow order, then builds the test with GCC and Clang (`-Wall -Wextra -Werror`, ASan + UBSan).
- **Windows build:** `.github/workflows/windows-portable.yml` builds it with clang-cl and runs it with the existing shader regression tests.

Local, after applying the patches to upstream `7b7f9a7` and fetching `3rdparty/xxHash`:
```sh
g++ -std=c++20 -Isrc -I3rdparty/xxHash -DXXH_INLINE_ALL tests/DriverCacheFileTests.cpp -o t && ./t
```

## What this does not do
It does not make shaders compile faster or change what the driver caches. It makes cache reuse (or its failure) visible in `_kyty.txt` so a second launch can be checked for `read and verified ... MB` followed by `loaded ... MB`. Those lines still need to be confirmed on the user's PC.

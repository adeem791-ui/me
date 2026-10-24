# Ghost of Yōtei rendering regression: state snapshot and experiment ledger

Opened 2026-10-24. **No behaviour change has been made for this investigation.** Diagnosis only.

## 1. Preserved state (nothing overwritten or deleted)

| What | Value |
|---|---|
| Working tree | `/home/user/me`, clean, no uncommitted changes |
| Checked-out branch | `claude/vram-pressure-relief` at `f8ab24f681a09ffcc8db760c55e38294994ff508` |
| Other local branches | `claude/async-cache-save` @ `c49bfe7` (**local only, not pushed, not touched**), `claude/prewarm-device-parity` @ `8dae700`, `claude/vulkan-loader-perf-audit-r5mhdb` @ `71b2e6f`, `main` @ `e15741c` |
| Read-only source copies used for diffs | `/tmp/claude-0/up/src` (upstream KytyPS5 clone, has both `7b7f9a7` and `9dcff95`), `/tmp/claude-0/up/y` (upstream `9dcff95` + consolidated patch 01–16 + patch 17) |
| User's installed emulator / game | **Not touched.** Nothing in this session can reach the user's PC. |

## 2. Build timeline (from Actions runs, verified against commit SHAs)

| Run | SHA | Upstream pin | Change | User-observed |
|---|---|---|---|---|
| **#26** | `2161a518e0a266dd5334f4b569599e8927d36c27` | `7b7f9a7` + patches 01–12 | misaligned tiled textures load as standalone images | **~2–3 frames + sound (last reported good)** |
| #27 | `7540cebb7e2933f207be64e902dfefab12ed6927` | `7b7f9a7` + 01–13 | patch 13: texture churn/upload logging | not individually reported |
| #28 | `f8c845611df5970b992f451e370dfbb8c4867c5c` | `7b7f9a7` + 01–14 | patch 14: thread OS-id logging | not individually reported |
| #29 | `0050c9d62b98f841c1e1dfdbc5d00c4a688de1de` | `7b7f9a7` + 01–15 | patch 15: fast-compile hint on every compute pipeline | not individually reported |
| #30 | `043908a5121aa72d5fc8b36c6aaaf4f3b8ce7bc8` | `7b7f9a7` + 01–16 | patch 16: VRAM eviction starts at 35 %/65 % instead of ~79 %/~89 % | not individually reported |
| **#31** | `be3be56058f76f3c0eda4bc18ab08c082f2745ff` | **`9dcff95` (+76 upstream commits)** + consolidated 01–16 | upstream rebase, 2 hand-resolved conflicts | not individually reported |
| #32 | `31886a3b2656e3ca9747b6237095f2b8cf4e49eb` | `9dcff95` + 01–16 + 17 | patch 17 VRAM relief + heartbeat (heartbeat needed perf panel) | not individually reported |
| **#33** | `f8ab24f681a09ffcc8db760c55e38294994ff508` | `9dcff95` + 01–16 + 17 | heartbeat gains `local_time`/`uptime_s`, logs without the perf panel | **the ~71-minute run (see §3)** |

Build #33's zip is `KytyPS5-Yotei-VRAM-Relief-Experimental`, run 40017034846,
`sha256:b101bbbdaa44552298f0212373dd44ecff782e3037bdd6b0ea859b9816e0eed6`.

**Which build produced the 71-minute log is inferred, not proven.** `VRAM relief` lines exist only in
#32/#33; `uptime_s` only in #33. Confirm by checking whether the heartbeat lines contain
`local_time=` (#33 only) and which unzipped folder was launched.

## 3. Reported evidence, re-read

From the ~71-minute run: 118 `vkCreateComputePipelines` (≈1.38 s total), **zero logged
`vkCreateGraphicsPipelines`**, no out-of-device-memory error, no fatal exit, VRAM relief enabled with
zero attempts, **3 heartbeats, last at uptime ≈37 s**, window closed normally; monitor CSV shows one
CPU thread near full use and very low average GPU busy.

The separate ~6-minute control run had VRAM relief **disabled** and is a different session; its
numbers are not mixed in here.

## 4. Two reported signals are measurement artifacts, not findings

**A. "Zero graphics pipelines" proves nothing.** `src/graphics/host_gpu/renderer/pipeline/shaders.cpp`:
the graphics trace at :575 (`begin`) and :586 (`done`) is wrapped in
`if (graphics_debug_dump_enabled())`, which needs `--graphics-debug-dump`
(`src/common/emulatorConfig.h:80`, `src/main.cpp:206`). The compute trace at :713 is an unconditional
`LOGF` (patch 08). So the log cannot show a graphics pipeline creation at all in a normal run.
Graphics pipelines may well have been created.

**B. Heartbeat silence does not prove a stall.** `graphicsRun.cpp:671` puts the 5-second
`LogMemoryStats()`/heartbeat behind `if (first_slice)`, and `first_slice = !submission.started`
(:633). A submission that does not complete is pushed back with `started = true` (:616), so **every
retry of a blocked submission has `first_slice == false` and logs nothing.** This gate is mine
(patch 10/17); the criticism of it is correct.

## 5. Ledger

| # | Hypothesis | SHA | Change | Expected | Actual | Status |
|---|---|---|---|---|---|---|
| H1 | The log's "0 graphics pipelines" means no host graphics pipeline was requested | `f8ab24f` | none (source read) | trace covers all creation paths | graphics trace is gated behind `--graphics-debug-dump`; compute is not | **CLOSED — evidence void** |
| H2 | Heartbeat stopping at 37 s means the GPU submission loop stalled | `f8ab24f` | none (source read) | heartbeat independent of submission state | gated behind `first_slice`, false on every retry | **CLOSED — evidence void** |
| H3 | A hand-resolved rebase conflict in `graphicsRun.cpp` changed queue behaviour | `be3be56` | none (diff vs upstream `9dcff95`) | behavioural delta | delta is opt-in profiling counters, the VRAM pressure check and the stats block only | **CLOSED — not the cause** |
| H4 | The `descriptors.cpp` hand-merge dropped patch 12's standalone-image fix (the fix that made #26 render) | `be3be56` | none (diff vs upstream `9dcff95`) | patch 12 path missing | patch 12's `standalone` branch and `FindStandaloneImage` are intact, after upstream's image-heap early return | **CLOSED — not the cause** |
| H5 | Upstream `a358b847` turns a flip wait into a silent head-of-line stall on queue 0 | `be3be56`+ | none yet (source read) | see §6 | structurally confirmed in source; not yet confirmed against a log | **OPEN — leading candidate** |
| H6 | Patch 16's 35 %/65 % eviction thresholds evict images later frames need | `043908a`+ | none yet | black/incomplete frames, image re-creation churn | not investigated | OPEN — secondary |
| H7 | Upstream's indirect-draw rework (`c513a4a1`/`949de19b`/`86fed10b`, `renderDraw.cpp` ±187 lines) suppresses draws | `be3be56`+ | none yet | zero/incorrect indirect counts | not investigated | OPEN — secondary |

A compile success is recorded separately from in-game behaviour. Builds #31–#33 compiled and passed
the shader regression tests; **none has been shown to render.**

## 6. H5 in detail: silent head-of-line stall on queue 0

Upstream `a358b847` ("stall waitUntilSafeForRendering in its command processor instead of the GPU
thread") changed `CommandProcessor::WaitFlipDone`:

```c
 void CommandProcessor::WaitFlipDone(uint32_t video_out_handle, uint32_t display_buffer_index) {
-	BufferFlush();
-	m_renderer.GetVideoOut().WaitFlipDone(...);          // blocked the GPU thread inline
+	if (!m_renderer.GetVideoOut().IsSafeForRendering(...)) {
+		SuspendPm4();                                     // returns Blocked; submission is requeued
+	}
 }
```

With that in place, in `graphicsRun.cpp`:

- `Process()` returns `complete == false`; the submission is `push_front`ed back onto its queue with
  `blocked = true` and `started = true` (:613–:617).
- The queue selector only ever looks at **`front()`** (:529), so while that submission sits at the
  head of queue 0 nothing behind it can run.
- Graphics submissions **and** `FlipPreparation` submissions both use **`queue_id = 0`** (:140, :162);
  compute uses queues 1+ (:154).
- When every queue front is blocked, the loop waits 100 µs, clears `blocked`, and re-processes the
  same submission — a poll loop that burns CPU on one thread.
- Nothing in this path logs, and `first_slice` is false, so the heartbeat is silent.

Predicted symptom set: no image; one CPU thread near 100 %; very low GPU busy; compute work continues
(so audio and compute pipeline creation keep going); no fatal error; heartbeats stop at the moment the
head of queue 0 first suspends. **That is the reported symptom set, point for point**, including the
118 compute pipelines all plausibly landing inside the first 37 seconds.

This mechanism cannot occur in build #26: at upstream `7b7f9a7`, `WaitFlipDone` flushed and blocked
inline, and never requeued. It is the only change in the #26 → #31 window that alters whether the
graphics queue makes forward progress.

**Not yet proven:** that `IsSafeForRendering` actually stayed false, and what it was waiting on. A
flip can also be retired by the presenter thread without queue 0 running, so the stall needs the
thing that would make the buffer safe to be *behind* the blocked submission. That is what the
diagnostic in §7 is for.

## 7. Next step (diagnostics only, no behaviour change)

Smallest set that distinguishes "no guest draws", "draws suppressed", "no host pipeline requested"
and "submitted but not presented", none of it gated behind `first_slice` or inside the suspended
path:

1. Log the graphics pipeline trace on the same terms as compute (ungate those two lines).
2. Count, per 5 s, from a timer thread rather than the submission loop: direct/indexed/indirect draw
   packets decoded, predicated draws and predicate results, indirect counts actually read, graphics
   pipeline lookup hits/misses/creates, queue submits, present results, flip counters, audio
   submissions/underruns.
3. One line when a submission suspends, naming the reason and the queue, plus the age of the current
   head of queue 0 — bounded (first N, then every Nth).

Expected result if H5 holds: a suspend line naming the flip wait, queue 0's head age growing without
bound, draw-packet counters frozen while compute counters advance.

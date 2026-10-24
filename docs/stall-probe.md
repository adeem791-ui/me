# Stall probe (patch 20)

**What it answers:** after the boot-state thread `GameState` ends, is the guest main thread
spinning in game code, waiting, or stuck on something the emulator should answer? And what exactly
do the long `wait_reg_mem` stalls wait for?

**Status:** written and tested, **not built, not run.** Diagnostic only: it changes no rendering, no
timing that the game can observe beyond the pauses described under *Cost*, and no error handling.

**Base:** upstream `9dcff95` + `01-16` + `17` + `18` (revision 3), the exact source of build #34
(source tree `07e954ad`). `patches/20-stall-probe.patch` (revision 3) is the only addition; with it
the source tree is `6e7e5d3c`. It does not need or include patch 19.

## Switches

| Variable | Effect |
|---|---|
| `KYTY_STALL_PROBE=0` | turns off both halves; nothing is recorded or logged |
| `KYTY_STALL_PROBE_JOIN=<name>` | which joined thread starts sampling (default `GameState`) |
| `KYTY_STALL_PROBE_CODE=0` | leaves out the `code` lines (a few bytes of the game's machine code) |

Nothing to set for a normal run: both halves are on by default in this diagnostic build.

## Half 1: full `wait_reg_mem` detail

FrameTrace keeps only the low 32 bits of the watched address and value. For every wait that lasts
200 ms or more, the probe logs:

```
WaitRegMemProbe: wait=2 begin local_time=… queue=0 submit=19 async_compute=0
  addr=0x0000002011880360 bits=32 condition=(value&mask)==ref func=3 ref=0x0000005f
  mask=0xffffffff value=0x00000000 masked=0x00000000 first_value=0x00000000 poll=10 wait_op=0
  waited_ms=200 evaluations=…
WaitRegMemProbe: wait=2 still_waiting local_time=… waited_ms=1000 … value=… condition=… ref=…
WaitRegMemProbe: wait=2 value_change waited_ms=… value=… masked=… previous=…
WaitRegMemProbe: wait=2 end result=satisfied waited_ms=12487 evaluations=… changes=…
  first_value=0x00000000 final_value=0x0000005f
```

(one line each in the log; wrapped here.)

- `addr` is the full 64-bit address, `bits` 32 or 64, `condition` the exact test
  (`(value & mask) <op> ref`), `func` the raw field (0 always, 1 `<`, 2 `<=`, 3 `==`, 4 `!=`,
  5 `>=`, 6 `>`).
- `still_waiting` at 1 s, 5 s, 30 s, 120 s, 600 s. `value_change` whenever the value read at a poll
  differs from the previous poll (16 per wait, 256 per run). `end result=superseded` means the
  processor went on to a different packet without this one passing.
- `queue` comes from FrameTrace (`-1` if FrameTrace is off).
- Bounds: 64 long waits per run, after which one line says so. Waits under 200 ms log nothing.

The command processor reads the value once and makes its own comparison exactly as before; the probe
only looks at what was read and decided.

## Half 2: samples of the guest main thread

Each successful join of `GameState` starts a **campaign**; the next join ends it and starts the next
(E2 had three joins: 19.4 s, 20.7 s and 39.6 s, so the first two campaigns are short and act as a
control). At most 6 campaigns per run.

A campaign is 13 **bursts**, at 0, 1, 2, 4, 8, 15, 30, 60, 120, 240, 480, 900 and 1800 s after the
join. A burst takes 32 samples 5 ms apart (about 0.16 s). Each sample suspends the main thread,
copies its registers, the 8 bytes each register points at, and the top 4 KiB of its stack, and
resumes it. Naming addresses, decoding call sites and logging all happen afterwards, while the thread
runs.

Per burst:

```
MainSampler: campaign=3 burst=5/13 t=+8s local_time=… verdict=guest_spin samples=32 guest=32
  guest_stub=0 emulator=0 os_wait=0 os_yield=0 host_other=0 unknown=0 failed=0 distinct_rips=3
  guest_span=0x1c cpu_pct=99.8 cpu_window_ms=4161 burst_cycles_per_us=3590 burst_ms=171.2
  pauses=32 pause_us_min=14.1 pause_us_median=21.7 pause_us_max=96.3 pause_us_total=788.0
  pause_pct_of_burst=0.460 already_suspended=0 resume_retries=0
  emulator_entry=[none x0] top=[guest eboot.bin+0x1a2b3c x18; guest eboot.bin+0x1a2b40 x9; …]
MainSampler: campaign=3 burst=5 sample=0 t=+8.002s paused_us=19.8 rip=0x… guest eboot.bin+0x1a2b3c
  rsp=0x… callers=[guest eboot.bin+0x0fe1d0 -> guest eboot.bin+0x1a2b00; …]
   … up to 8 sample lines …
MainSampler: campaign=3 burst=5 code guest eboot.bin+0x1a2b3c x18 from 0x…
  (16 bytes, then | at the sampled address, then 32): … 8b 07 85 c0 | 74 f8 …
MainSampler: campaign=3 burst=5 regs sample=0 rax=0x… rcx=0x… … r15=0x… rip=0x… rflags=0x…
MainSampler: campaign=3 burst=5 reg_targets sample=0 … [rdi]=0x0000000000000000 …
MainSampler: campaign=3 burst=5 wait_reg_mem_match sample=0 no_register_near=inconclusive (…)
  (recent long wait addresses: 0x…)
…
MainSampler: campaign=3 end reason=schedule_done local_time=… samples=416
  main_thread_paused_us_total=9012.4 pause_us_max=180.2
```

(The numbers above are made up to show the format.)

The `code`, `regs`, `reg_targets` and `wait_reg_mem_match` lines appear only when the busiest place
changes, so a stuck thread gives them once. A campaign therefore logs at most about 170 lines.

Where an instruction pointer can be:

| Kind | Meaning |
|---|---|
| `guest` | a loaded game or system module (`eboot.bin+0x…`, `libc.prx+0x…`) |
| `guest_stub` | code the emulator generated inside a module's mapping (instruction trampolines, TLS handler) |
| `emulator` | the emulator's executable; `near_hle=` names the closest HLE function at or below (a landmark only) |
| `os_wait` | a Windows system call that blocks (`ntdll!NtWaitForSingleObject+0x14`, `NtDelayExecution`, `NtWaitForAlertByThreadId`, …) |
| `os_yield` | `ntdll!NtYieldExecution` (gives up the CPU but stays runnable) |
| `host_other` | other DLLs: C runtime, GPU driver, system libraries (nearest export named) |
| `unknown` | anonymous or unmapped memory |

`callers=[…]` lists the first three return addresses into game code found on the stack, each with
what it called: `-> HLE LibKernel::PthreadCondWait` when the call goes (directly, through a PLT
entry, or through a pointer) to an emulator function, `-> guest …` for a game function,
`-> (indirect)` when the target was in a register.

## How the samples tell the three cases apart

The verdict word is a reading aid; the sample lines are the evidence. Three measurements decide:

1. **Where the instruction pointer is** across 32 samples (game code, emulator, a wait call).
2. **How much CPU the main thread used** between bursts (`cpu_pct`, from the thread's own CPU
   time, independent of the external monitor) and during the burst (`burst_cycles_per_us`: near
   the CPU clock rate when running flat out, near 0 when blocked).
3. **What the game called**, from its own call instructions on the stack (`callers`,
   `emulator_entry`).

| Case | Instruction pointer | CPU | Callers / registers | Verdict |
|---|---|---|---|---|
| **Guest busy loop** | ≥ 90% in game code, a few addresses within a few bytes of each other (`guest_span` small, `distinct_rips` small) | ~100% | no emulator function under it; the `code` bytes show the loop, `reg_targets` the memory it reads | `guest_spin` |
| **Wait** | ≥ 80% in a blocking system call | ~0% | `callers` names the emulator function the game called (for example a condition wait or an event-queue wait) | `waiting` |
| **Missing emulator response, polled** | alternating game code and one emulator function | high | the same `HLE …` entry in most emulator samples: the game keeps asking and gets the same answer | `polling_emulator` |
| **Missing emulator response, emulator spins** | ≥ 80% inside emulator code (short waits included) | high | `callers` names the emulator function the game is inside | `busy_in_emulator` |
| Progress | many different places | any | varied | `varied` |

A `guest_spin` does not by itself clear the emulator. A game loop that polls memory is waiting for
whoever writes that memory. The next step is to read the loop's `code` line and `reg_targets`: if
the memory it polls is something the emulator should write (a GPU label, an end-of-pipe write, a
video-out or event flag), the cause is still a missing emulator response. If the memory is written
by another game thread, look at that thread next.

`wait_reg_mem_match` compares each register with the recent long `wait_reg_mem` addresses. A
register within 0x100 bytes is printed as `lead:`, a reason to look closer, not proof.
`no_register_near=inconclusive` means only that no register held such an address at that instant:
the game may compute the address from a base and an offset, or keep it in memory rather than in a
register. It does not rule out a link between the CPU loop and the GPU handshake.

A `waiting` verdict names what the main thread waits in. Whether the emulator owes a signal there
follows from that function and its object.

E1 and E2 showed the main thread at about 100% CPU after the join (external monitor). If that
holds, expect `guest_spin`, `polling_emulator` or `busy_in_emulator`, not `waiting`; the samples
confirm or overturn it.

## Cost and safety

- The main thread is suspended 32 times per burst, each time for the duration of a few system calls
  (register read, page queries, up to 15 eight-byte reads and a 4 KiB copy), expected to be tens of
  microseconds. Bursts are seconds to minutes apart.
- **The cost is measured, not assumed.** Each pause is timed from just before `SuspendThread` to
  just after `ResumeThread` returns (an upper bound: it includes any time the sampler itself was
  descheduled meanwhile). Each sample line has `paused_us`; each burst line has the minimum,
  median, maximum and total, and the total as a share of the burst's wall time
  (`pause_pct_of_burst`); each campaign's end line has its total and maximum.
  `already_suspended` counts samples where the thread was already suspended by something else.
- **Every `ResumeThread` is checked.** A failure is retried up to 4 times (a failed call leaves the
  suspend count unchanged, so a later success resumes exactly once; `resume_retries` counts
  retries). If all fail, the sampler first, in one step under its own lock, disables sampling for
  the rest of the run and drops any campaign a newer join has queued; only then, with that lock
  released, does it log one `ResumeThread FAILED` line (`queued_campaign_dropped=1` if one was
  dropped) and end the campaign with `reason=resume_failed`. The worker checks the disabled state
  before starting any campaign, and later joins are refused (`not sampled: sampling stopped
  earlier`), so nothing samples the thread again even if that log call never returns. It never
  returns only if the frozen thread holds the logger's lock; the log would then stop at the last
  sample line while the game hangs, with the external monitor showing the main thread at 0% CPU.
- While it is suspended the sampler only makes system calls: no allocation, no lock, no logging, so
  it cannot deadlock on a lock the main thread holds. Memory is read with `ReadProcessMemory` after
  `VirtualQuery`; guard pages and no-access pages are skipped, so the read cannot fault, cannot
  disturb a thread's stack growth, and never enters the emulator's memory-tracking handlers.
- Module and HLE names are copied under the loader's lock (`SnapshotProgramRanges`,
  `SnapshotHleFunctions`), so a module unloading meanwhile cannot leave a dangling pointer.
- Windows only. On other platforms it logs once that nothing is sampled.

## Limits

- 32 samples 5 ms apart describe a loop well, not a function that changes every few milliseconds.
- `callers` is a stack scan, not an unwind: a stale return address left in a frame can appear. A call
  instruction must precede each address, which removes data that merely points into code.
- `near_hle` is the closest HLE function below the address; non-HLE emulator code sits between HLE
  functions, so only `HLE …` from a caller line is an exact name.
- The `code` bytes are a few instructions of the game. They stay in the local log; keep that log
  private rather than posting it publicly, or set `KYTY_STALL_PROBE_CODE=0`.

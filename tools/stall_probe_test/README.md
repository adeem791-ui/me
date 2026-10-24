# stall_probe_test

Tests for `patches/20-stall-probe.patch`: the `wait_reg_mem` tracker (`waitRegMemProbe.h`) and the
platform-independent part of the main-thread sampler (`mainThreadSampler.h`). Not part of the
emulator build. `stub/` replaces the emulator's logger with one that keeps each line, so tests can
read what was logged; it formats with `snprintf`, so the compiler checks every format string.

The Windows sampling itself (`mainThreadSampler.cpp`: SuspendThread, GetThreadContext, module
lookup) cannot run here; it was syntax-checked with MinGW-w64 headers, including a pass that maps
`LOGF` to `printf` so each format string is checked against its arguments.

From a KytyPS5 checkout at `9dcff95` with `01-16`, `17`, `18` and `20` applied:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Wformat=2 -g -O1 -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I<this dir>/stub -Isrc -I3rdparty/Vulkan-Headers/include \
    <this dir>/stall_probe_test.cpp -pthread -o stall_probe_test
./stall_probe_test        # prints: stall probe tests: all passed
```

| Test | Behaviour |
|---|---|
| `TestDisabledLogsNothing` | with `KYTY_STALL_PROBE` off, a thousand unsatisfied evaluations log and publish nothing |
| `TestEnvironmentSwitch` | `0` reads as off; `1` and unset as on |
| `TestShortWaitIsSilent` | a wait that passes within 200 ms logs nothing |
| `TestLongWaitFullDetail` | begin line has the full 64-bit address, bits, condition, ref, mask, value, queue, submit, poll; still-waiting at 1 s and 5 s; value change; end with duration, evaluations, changes, first and final value |
| `Test64BitValues` | 64-bit waits print 16 hex digits; unknown queue prints -1 |
| `TestCompareOperators` | function field 0–6 and out of range |
| `TestSupersededAndNewSubmission` | the same packet in a new submission is a new wait; a different packet ends the old one as `superseded` |
| `TestLongWaitBudget` | 64 waits logged, one budget line, no end lines for unlogged waits; every long wait still published for the sampler |
| `TestValueChangeCaps` | 16 change lines per wait, 256 per run; the end line still counts every change |
| `TestMilestonesSkippedTogether` | a slow poll that passes several milestones gives one line |
| `TestQueueUnknownWhenFrameTraceOff` | queue -1 when FrameTrace is off |
| `TestClassifyHost` | wait, sleep, alert, I/O-completion and message waits are `os_wait`; `NtYieldExecution` is `os_yield`; same names outside ntdll/win32u are not |
| `TestDecodeCallBefore` | `E8`, `FF 15`, register and memory-operand calls (with REX, SIB, disp8, disp32); padding, `mov`, `jmp` are not calls |
| `TestStackScanAndCallee` | return addresses found in stack order; data pointers and emulator-generated code skipped even after call-shaped bytes; callee read through a PLT entry and through a pointer |
| `TestNearestExport` | nearest named export at or below an RVA; forwarders skipped; truncated and 20,000 randomly corrupted images handled without reading out of bounds (AddressSanitizer) |
| `TestNearestFunction` | nearest HLE function at or below an address |
| `TestVerdicts` | each verdict and its boundaries, including CPU unknown |
| `TestTopKeys` | commonest first, ties in first-seen order |
| `TestResumeWithRetry` | success on the first call; two failures then success resumes exactly once; constant failure stops after the allowed attempts with the last error |
| `TestSummarizePauses` | minimum, median (odd and even counts), maximum, total; empty input |
| `TestQueuedCampaignDroppedOnResumeFailure` | a join queued during a burst whose resume fails every retry: the queued campaign is dropped, the worker stops by itself, the thread is never sampled again, the retry count is unchanged, the failure is logged only after sampling is disabled and with the queue's lock free; later joins are refused (reported once) and no worker restarts |
| `TestNextRefusesWhenDisabled` | the worker refuses to start a campaign on the disabled flag alone, even with a request pending; a wait returns at once |
| `TestCampaignQueueBasics` | normal operation after the refactor: a newer join replaces the pending one, one worker start, campaign cap reported once, a newer join interrupts a wait, shutdown |
| `TestBounds` | burst schedule ascending and within 30 minutes; samples and log lines per campaign bounded |

Also run with `-fsanitize=thread` (GCC) and with clang `-O2`; both pass. A mutation check (18 deliberate
bugs in the tracker, call decoding, export parsing, stack scan, verdicts, resume retry, pause
summary and the resume-failure stop) fails the tests every time.

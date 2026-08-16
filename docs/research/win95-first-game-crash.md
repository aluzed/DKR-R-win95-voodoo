# The game's first crash, and how it let itself be read

Measured on 15 August 2026 on the test machine, with the ROM.

## Where the port stands

The game starts. The runtime's log says so unambiguously:

    [boot][rom] validated and registered
    [boot] runtime initialized; waiting for first safe VI state
    [boot][audio] frequency=48000 (diagnostic backend)
    [boot] VI initialized; starting recompiled DKR entrypoint
    [boot][audio] frequency=22050 (diagnostic backend)
    [boot][vi] present=4260

The ROM is validated, the recompiled code's entry point is reached, and the game
reconfigures the audio frequency from 48000 to 22050 itself — that is, it runs its
own initialisation, not merely the runtime's.

## It took three tools before anything could be diagnosed

**`stderr` was not recoverable.** The whole log goes through it and Windows 95's
COMMAND.COM has no `2>&1` syntax. The runtime now redirects it to a file on this
target, unbuffered.

**The game had no exception filter at all.** `game_main.cpp` disables its own on
this target on the grounds that `platform/win95/startup.c` "already installs its
own filter". That was true of the platform witness and false of the game:
`dkr_win95_startup` was only called by `witness.c`. The symptom was a Windows
"illegal operation" box, no trace, and **the video mode not restored** — the most
serious of the three, the Voodoo holding the screen through its analogue relay.

**ScanDisk was swallowing the keystrokes** at every boot following a crash, which
made it look as though the program did not start when it had never been launched.
`AutoScan=0` removes the cause.

## First defect: RDRAM was too small for librecomp's layout

    *** unhandled exception ***
      code    : 0xC0000005 (invalid memory access)
      address : 0x007B4226        -> o1heapInit + 0x46

The address falls in the loop that clears the instance's bins
(`out->bins[i] = NULLFRAGMENT`) — that is, on the **heap's first byte**.

Patch 0017 had sized RDRAM on the *game*'s needs: DKR's pool stops at `RAM_END`,
0x80400000, and the decomp does not use the Expansion Pak. That reasoning is right
about the game and wrong about librecomp, which places its own regions well above:

    0x80800000  PI handles         8 MiB
    0x80801000  patch area
    0x81000000  mod area          16 MiB

and `init_heap` places the heap at `mod_rdram_start`. With 4 MiB committed and
8 MiB reserved, that write fell **outside the reservation entirely**.

The layout is left as it is and the sizes follow it: 20 MiB committed place the
heap at 16 MiB with 4 MiB of usable arena, and 24 MiB reserved keep 4 MiB of
protected pages above, so that an out-of-range guest address still raises a fault.
ADR 0003 records 47 MiB free on this machine: that is affordable.

A `static_assert` now forbids dropping back below `mod_rdram_start`.

## Second defect: a null pointer in the game's RSP handler

The exception filter was enriched to report the **touched** address and the
registers, and not only the code's address. The difference is decisive on a port
whose entire guest memory space is an indexed array:

    address : 0x006A98D7        -> __scHandleRSP + 0x97
    touching: 0x82360010 for reading
    ecx=00000000   ebp=02360000

The instruction is `mov -0x7ffffff0(%ebp,%ecx,1),%edx`, the typical shape of the
recompiled code: `ebp` carries the RDRAM base, `ecx` the guest address, and the
displacement folds the KSEG0 bias together with the field being read.

`ecx` is **zero**. The guest address is therefore `0x80000010`, and the field at
offset 0x10 of an `OSScTask` is `list`. In other words:

    sc->curRSPTask->list   with curRSPTask null

The game receives an RSP task completion **while it has no current task**.

That is not a defect of the game: it is our signalling. Six of this port's patches
already bear on the SP and DP completion order — 0006, 0009, 0010, 0011, 0012,
0013 — because that area is delicate. One SP interrupt too many, or one delivered
after the game has given the task up, produces exactly this.

That is the next point to deal with, and it is now **named** rather than
suspected.

## What this session changed in the method

An exception filter that reports the code's address without the touched address
forces one to disassemble by hand to guess what was missing. With both, plus the
registers, the fault reads itself: here, three lines sufficed to go from
"somewhere in the RSP handler" to "`curRSPTask` is null".

## The trace eliminates the most likely hypothesis

`DKR_TRACE_SP` counts the task submissions and the SP and DP edges. On the
machine, before the crash:

    [trace][sp] submit   type=2  submitted=1 sp=0 dp=0
    [trace][sp] sp       type=0  submitted=1 sp=1 dp=0

**One submission, one SP edge**, then the fault. There is no double delivery.

`type=2` is `M_AUDTASK`: the very first task DKR submits is **audio**, not
graphics. It goes into the scheduler's command queue
(`osSendMesg(osScGetCmdQ(gAudioSched), t)`, `audiomgr.c:363`), so it really is
libultra's `__scExec` that starts it — and it is `__scExec` that sets
`sc->curRSPTask`.

That leaves the race hypothesis: the emulated RSP finishes before the submitting
thread has completed its bookkeeping, which real hardware does not allow — the
interrupt arrives there microseconds later. Six of this port's patches already bear
on that ordering, which made it plausible.

It is false. Publishing the edge one millisecond later changes **nothing**:

    address : 0x006A98D7        (identical)
    touching: 0x82360010        (identical)
    ecx=00000000 ebp=02360000   (identical)

Identical fault, identical registers. The state is **deterministic**, not a race.
The delay was therefore removed: a change that fixes nothing while modifying the
scheduling is worse than no change at all.

What that leaves: either `curRSPTask` is never set — so `__scExec` does not take
the path we think — or it is cleared in between by a second pass through
`__scHandleRSP` that the trace does not see, the trace counting our edges and not
the messages the game consumes.

It is on the game's side that one must look now, not on ours.

## The memory dump: the structure is empty

The filter now dumps sixteen words from any register that looks like a guest
address — high byte `0x80` — translating through the RDRAM base. The report becomes
readable without attaching a debugger to a machine that has none.

    esi -> 0x80121260 :
      +00  00000100 00000000 00000000 00000000
      +10  00000000 00000000 00000000 00000000
      +20  00000400 00000000 00000000 00000000
      +30  00000000 00000000 00000000 00000000

`esi` is `__scHandleRSP`'s first argument, hence the `OSSched`. It is **almost
entirely null**. The only two non-null values, `0x100` at `+00` and `0x400` at
`+20`, look like sizes or flags, not like queue or task pointers.

> **That reading was wrong, and the correction is worth keeping.** An `OSSched`
> begins with its two message templates — `retraceMsg` and `prenmiMsg`, 32 bytes
> each — then a queue, a buffer, a second queue, a second buffer, and an embedded
> `OSThread` of 432 bytes. `curRSPTask` lives at offset **0x274**. The sixty-four
> bytes dumped therefore showed nothing but the header, and concluding "the
> structure is empty" amounted to concluding about something other than what was
> being looked at. The dump now covers 640 bytes.

The address is the right one: `eax` is `0x024814D4`, that is exactly
`gMainSched + 0x274` once the RDRAM base is subtracted. The code was indeed reading
`curRSPTask`.

## The scheduler's real state

Dumped up to offset 0x280, and read by reversing each word — the guest RDRAM is
stored byte-swapped on the host side:

| Offset | Field | Value |
|---|---|---|
| 0x260 | `clientList` | **0x80116220** |
| 0x264 | `audioListHead` | 0 |
| 0x268 | `gfxListHead` | 0 |
| 0x26C | `audioListTail` | 0 |
| 0x270 | `gfxListTail` | 0 |
| 0x274 | `curRSPTask` | 0 |
| 0x278 | `curRDPTask` | 0 |

**The scheduler is duly initialised**: `clientList` points at a registered client,
and the embedded `OSThread` is in place. So it is neither an empty structure nor a
wrong address.

But **all four task lists are empty**, in addition to the two current tasks. The
RSP task completion message arrived while the scheduler had a task **nowhere** —
neither in progress nor waiting.

That moves the question. It is no longer "why was `curRSPTask` cleared" but **"why
did the RSP start a task the scheduler never took out of its command queue"**.

Now `submit_rsp_task` is only called from librecomp's `osSpTaskStart`, and within
libultra only `__scExec` calls it — after removing the task from `cmdQ` and
chaining it into one of the lists. The lists being empty, `__scExec` did not run.

Something therefore starts the task without going through the scheduler.

The two remaining leads, in the order in which they are testable:

1. `osCreateScheduler` did not write where the game believes it did. Checking that
   requires tracing the call on the guest side, which `librecomp` allows through
   its exports.
2. The structure really is at that address but its content was cleared, for
   instance by an RDRAM snapshot copied over it — `submit_rsp_task` copies 8 MiB of
   RDRAM per graphics task, and the order of those copies deserves a look.

The second is cheap to rule out: the trace shows that no graphics task had yet been
submitted at the moment of the fault.

## Two more hypotheses, eliminated

**The SP and DP messages are distinguishable.** The scheduler registers one queue
and one message per event; if they carried the same value, the game would treat a
DP completion as an RSP completion and would enter `__scHandleRSP` twice — whose
first pass clears `curRSPTask` before dereferencing it. That was a complete
explanation of the crash. It is false:

    sp.mq=0x801212A0 sp.msg=0x0000029B
    dp.mq=0x801212A0 dp.msg=0x0000029C

Identical queues — it really is the `interruptQ`, at `gMainSched + 0x40` — but
distinct messages.

**`__scExec` does write `curRSPTask`.** The recompiled code, at guest address
0x8007A030, does the store **in a `bne`'s delay slot**, hence on both paths:

    bne  $s0, $s1, L_8007A038
    sw   $s0, 0x274($t9)      <- delay slot, executed whatever happens

The field is therefore filled in after the task starts.

## The stack, and what it establishes

Windows 95 has no `StackWalk64`, and the recompiled code has no usable stack frame.
The report therefore walks the stack and keeps whatever looks like a code address —
not an exact call stack, but a list of candidates, which is infinitely better than
nothing when one does not know how one got there.

    0x006AB6A6  __scMain + 0x466
    0x00777101  run_thread_function + 0xE1
    0x007D9DFA  _thread_func + 0x21A
    0x00866CBF  dkr::win95::thread::entry<...> + 0x2F
    0x00834C01  dkr_thread_trampoline + 0x21

The chain is confirmed: `__scHandleRSP` is indeed called from `__scMain`, on the
scheduler's thread, itself carried by E02-S01's threading layer.

## What remains, and why it is now the chief suspect

The count runs as follows: one submission, one `sp_complete` on our side,
distinguishable messages, a `curRSPTask` written after the start — and yet
`__scHandleRSP` finds it null.

That leaves only one possibility: **the game receives the message more than once**.
Our trace counts our calls to `sp_complete`, not the messages actually deposited
into the guest queue. A duplicate deposit would be invisible to it.

This port's patch 0013 replaces precisely the transport of external messages with a
"reliable" queue. That is where to look, and the measurement to make is simple:
count the deposits into the guest queue, not the calls that request them.

## The starvation was real, and it is fixed

The deposit counter, once its cap was made **per message value**, gives the answer:

    msg=0x0000029B requeued   deposits=68 requeues=1 refusals=26
    msg=0x0000029B deposited  deposits=71 requeues=1 refusals=35

One `sp_complete`, one deposit — no duplicate. But the SP edge was first **refused
and requeued**, then deposited nine refusals later. It was stuck behind the flow of
retraces in an eight-slot queue.

The game, for its part, does not wait: a few frames without an answer and its
scheduler gives the task up and resets `curRSPTask` to null. Our message arrives
afterwards, and `__scHandleRSP` dereferences a null pointer.

On the hardware, an SP interrupt and a scan retrace are two independent events
whose relative order is not guaranteed. Serving them before the retraces is
therefore faithful, and suffices to lift them out of starvation — their order among
themselves is preserved, and that is the one the game observes.

**Measured effect**: the SP message is now deposited at the first attempt, without
being requeued. And the fault **moves** to `__scHandleRDP`, which is the best proof
that the starvation was real: the game goes further and meets the next problem.

## A zero-byte log that contained everything

The next crash produced an empty `DKRR.LOG` — while the trace it contained was
exactly what we were after.

The runtime redirects `stderr` unbuffered, so the bytes go to the system as they
come. But Windows 95 only updates the **size in the directory entry** at close
time: a process that dies leaves a zero-byte file whose content is nonetheless on
the disk, and invisible to any tool that reads the table.

The exception filter therefore closes `stderr` before writing its own report. The
log went from 0 to 1446 bytes on the next crash.

## Where the count stands

After the fix, on the last run: four retraces, **one** SP edge deposited at the
first attempt, **no** DP edge ever deposited — and yet a fault that passes through
`__scHandleRDP` before coming back to `__scHandleRSP`.

The game is therefore waiting for an RDP completion we do not emit. DKR's audio
task carries `OS_TASK_DP_WAIT` in its task flags, which is the lead to follow.

## Coalescing the retraces: a fix that regresses

The analysis suggested what to do next: since the queue saturates with retraces, do
not deposit a second one while the first has not been delivered. On the hardware, a
retrace missed while the processor is busy is simply missed.

**The game no longer starts.** It stops at the heap's initialisation:

    Initializing recomp heap at offset 0x01000000 with size 0x00400000

and does not display a single frame. The "a retrace is pending" flag stays set, and
every subsequent one is discarded: the game waits for a wake-up that never comes.

The likely cause is that `dequeue_external_messages` is only called from a waiting
guest thread. Before the game runs, nobody drains — the first retrace sets the flag
and nothing clears it. Previously the retraces piled up in our queue and were
delivered in a burst at the first drain.

The change is reverted. A fix that regresses is worse than the defect it aims at,
and this one traded a late crash for an immediate hang.

What the failure teaches, and which is worth keeping: **our external queue is not
drained at regular intervals**, but opportunistically, when a guest thread waits.
Any deposit policy that assumes periodic draining is therefore wrong by
construction. The right shape remains to be found — probably by reserving slots
rather than discarding messages.

## Reserving slots rather than discarding messages

The correct shape was constrained by the previous failure: hold nothing back
between two passes, since our external queue is not drained at regular intervals.

So we look at the guest queue's real state **at deposit time**. If fewer than two
slots remain free, a retrace is not deposited. On the hardware, a retrace raised
while the queue is full is lost in the same way — `osSendMesg` is called there
without blocking, from the interrupt.

### What that changes

| | before | after |
|---|---|---|
| tasks submitted | 1 | **170** |
| SP edges | 1 | **169** |
| DP edges | 0 | **61** |
| display lists | 0 | **547** |
| normalised audio voices | 0 | **17** |
| frames presented | ~780 | **3540** |

The game runs its audio engine and **submits display lists**. It is the first
moment in this port where DKR really does its work on Windows 95.

The crash remains, further on, in `__scHandleRDP` — but after 170 tasks instead of
one.

### What the trace said, and what had to be read into it

The saturation did not show in the high-level counters: one task submitted, one SP
edge, everything looked consistent. It only showed by counting **the effective
deposits into the guest queue**, and by giving every message value its own trace
cap — without which the retrace, sixty times a second, devoured the budget before
anything interesting arrived.

## What remains: the DP edge, and why it is subtler

The crash remains in `__scHandleRDP`, with a null `curRDPTask` read at offset 0x4.
But it now occurs **after 170 tasks**, not after one: it is an occasional condition,
not a systematic defect.

The decomp shows why it is more delicate than the SP case. At the end of
`__scHandleRSP`, the scheduler **already starts the next task**:

```c
state = ((sc->curRSPTask == 0) << 1) | (sc->curRDPTask == 0);
if ((__scSchedule(sc, &sp, &dp, state)) != state)
    __scExec(sc, sp, dp);
```

And `__scExec` only writes `curRDPTask` when the RSP task and the RDP task are
**the same** — the store is on the `bne`'s untaken path, unlike `curRSPTask` which
is in the delay slot. An audio task, which asks only for the RSP, therefore leaves
`curRDPTask` unchanged.

Three possible causes, indistinguishable without measurement:

1. We emit a DP edge for a graphics task the game has not registered as needing the
   RDP. The graphics path calls `dp_complete()` unconditionally after
   `sp_complete()`.
2. Two DP edges for the same task.
3. The same starvation as for SP, but residual: the reservation keeps two slots,
   and an SP+DP pair asks for exactly two — if one submission by the game takes one
   in between, the DP is refused.

The third is the most likely given the profile: occasional, and tied to pressure on
the queue. It is tested by counting the refusals per source, which the trace
already knows how to do.

What is gained in the meantime: the game submits 547 display lists and runs its
audio engine before getting there.

## The starvation is entirely gone, and it is no longer the explanation

The totals per message, over the whole run:

| message | deposited | refused | requeued |
|---|---|---|---|
| retrace | 2984 | **0** | 0 |
| SP | 1274 | **0** | 0 |
| DP | 545 | **0** | 0 |

**Not a single refusal.** The hypothesis of a residual starvation on the DP edge is
therefore eliminated: it is never discarded. And 545 DP edges for 550 display lists
is consistent — no doubling either.

That leaves causes 1 and 2: a DP edge emitted for a task the game has not
registered as needing the RDP, or a race in which the game clears `curRDPTask` by
another path before our message arrives.

### A false alarm, and always the same cause

For a moment the figures seemed to accuse a multiplication: 1274 deposits of the SP
message for 172 calls to `sp_complete`. It was an artefact.

The `[trace][sp]` lines stop being printed beyond a few hundred events; their last
display therefore shows the state at that moment, not the total. I was comparing **a
capped counter against an uncapped one**.

It is the third time in this investigation that a measurement artefact imitates a
defect, and all three times the cause is the same: two quantities compared without
their observation budgets being comparable. The trace now keeps both forms — the
first occurrences for the chronology, the periodic totals for the rest of the run.

## A defect in my own fix, invisible in the counts

The probe placed to separate the two remaining causes **never fired**, while 544 DP
messages were being deposited. It was conditioned on the message's source; the
deposit trace was conditioned on the value.

`enqueue_external_message_src` did not copy the source into the queued message.
Everything passing through it therefore carried the default value, and **the two
policies written just above missed their target**:

- the priority given to the SP and DP edges promoted only SP, the only one to go
  through the expected variant;
- the slot reservation meant for the retraces could discard a DP edge, which then
  looked like its twin.

The defect did not show in the counts: nothing was refused, so nothing seemed to be
missing. It only showed because a probe refused to fire.

It is the fourth time in this investigation that an instrument reveals something
other than what it was looking for — and the first where it reveals a defect in the
fix that precedes it.

## What the probe establishes once repaired

    curRDPTask at deposit = 0x405F1280  (nulls=0 set=1)
    ...
    last: nulls=0 set=500

**`curRDPTask` is never null at the moment we deposit the DP edge** — 500 deposits,
no nulls. The pointer is therefore cleared **between our deposit and the game's
handling of it**.

So it is neither a lost message, nor an extra message, nor a message for a task
that does not need the RDP: all three hypotheses fall. It is a state transition of
the game itself, between the queuing and the handling.

The next lead is in `__scHandleRSP`: it ends by calling `__scExec` to start the next
task, and `__scExec` only writes `curRDPTask` when the RSP task and the RDP task are
the same. An RSP-only task, started between our deposit and its handling, therefore
leaves `curRDPTask` at whatever the last `__scHandleRDP` put there — that is, zero.

As things stand: 555 display lists, 3720 frames, no message refused.

## The gap between the two edges is not the cause either

The graphics path publishes the SP edge **before** parsing the display list — that
is patch 0009's choice — and the DP edge afterwards. The gap therefore covers the
whole rendering time, during which the game can start several tasks, since
`__scHandleRSP` ends by calling `__scExec`.

A plausible hypothesis, and a false one. Putting the two edges side by side changes
**nothing**:

    address : 0x006AA58C   -> __scHandleRDP + 0x6c   (identical)

The change was reverted: it modifies the scheduling without benefit, and patch 0009
exists for a reason.

## The state of the investigation

Five hypotheses eliminated by measurement, in the order in which they seemed most
likely:

1. double delivery of the SP edge — one deposit for one call;
2. a scheduling race — delaying the edge changes nothing;
3. an uninitialised scheduler structure — it is initialised, I was looking at its
   header instead of the field;
4. indistinguishable SP and DP messages — their values differ;
5. residual starvation on the DP edge — no message is refused any more;
6. the gap between the SP and DP edges — putting them side by side changes nothing.

What is established: `curRDPTask` is **always set at the moment we deposit** the DP
edge, and null when the game handles it. The clearing therefore happens inside the
game, between the queuing and the handling, and none of the delivery policies tried
influences it.

The next measurement must therefore bear on the game and not on us: instrument
`__scExec` and `__scHandleRDP` on the guest side to see which transition clears the
field. The recompiled code does not lend itself to `printf`, but the address of
`gMainSched + 0x278` is known — a watch on that word, sampled from the runtime,
would say when it goes to zero.

## DKR sends its tasks into the interrupt queue

`rcp_dkr.c` does not go through the scheduler's command queue:

```c
osScInterruptQ = osScGetInterruptQ(sc);      /* gfxtask_init */
...
osSendMesg(osScInterruptQ, dkrtask, OS_MESG_BLOCK);
```

The graphics tasks therefore go **straight into the interrupt queue**, where
`__scMain` picks them up through its `default:` case and chains them with
`__scAppendList`.

That eight-slot queue thus carries four things at once: our retraces, our SP and DP
edges, the scheduler's internal messages, and the game's task pointers — the last of
those sent **blocking**.

That explains retrospectively the scale of the slot reservation's effect. It was not
merely a delayed interrupt: when the queue saturated, the game's graphics thread
**blocked** while trying to submit its task. The model I had in mind — "our messages
delay its own" — was too optimistic: our messages *stopped* the game.

## The other discrepancy noted in passing

`func_80079760`, Rare's addition to the scheduler, calls `__scYield` as soon as
audio is waiting while an RSP task is running. DKR therefore **interrupts its
graphics task** to let the audio through.

Our runtime ignores yields: `osSpTaskYield` is empty and `osSpTaskYielded` always
returns zero, with the comment "acts as if the task had finished before receiving
the request". `__scHandleRSP`'s `if (osSpTaskYielded(...))` path is therefore never
taken, and an interrupted task is treated as completed.

That is not necessarily the cause of the remaining crash, but it is a place where
the game's model and ours diverge plainly, on a mechanism DKR really uses.

## The yield is not taken

`func_80079760` calls `__scYield` as soon as audio is waiting while an RSP task is
running, and our runtime ignores yields. The divergence is real in the code; what
remains is whether the game takes it.

Counters placed in `osSpTaskYield_recomp` and `osSpTaskYielded_recomp`: **neither
function is ever called** over a sequence of 577 display lists.

A seventh hypothesis eliminated. The divergence exists but sleeps — it could wake up
mid-race, where the audio is more heavily loaded, and it will have to be
reconsidered then. It does not explain the current crash.

The counters were removed: keeping a permanent probe on a dead path costs a
dependency patch for nothing. The reasoning stays recorded here — that is what will
save redoing the measurement.

## The investigation so far

Seven hypotheses eliminated by measurement, each having seemed the most likely at
the moment of being tested:

| # | Hypothesis | What ruled it out |
|---|---|---|
| 1 | double delivery of the SP edge | one deposit per call |
| 2 | a scheduling race | delaying the edge changes nothing |
| 3 | an uninitialised scheduler | it is initialised; I was reading its header |
| 4 | SP and DP messages conflated | their values differ |
| 5 | residual starvation on DP | no message refused any more |
| 6 | the gap between the SP and DP edges | putting them side by side changes nothing |
| 7 | the ignored yield | the game does not yield |

Two real causes found and fixed along the way: RDRAM too small for librecomp's
layout, and the saturation of the interrupt queue — the latter **blocking** the
game's graphics thread, since DKR sends its tasks there with `OS_MESG_BLOCK`.

What stays established and unexplained: `curRDPTask` is always set when we deposit
the DP edge, and null when the game handles it.

## An eighth elimination, and a contradiction that holds

If `curRDPTask` is set at the deposit and null at the handling, and only
`__scHandleRDP` clears it, then another DP edge must have been handled in between —
hence one waiting in the queue.

Counted directly, by walking the live messages in the guest queue at every deposit:
**no DP edge is ever already waiting**. Zero collisions over the whole run.

The contradiction therefore holds, and it is now precise:

- `curRDPTask` is non-null at **every** deposit of the DP edge;
- no second DP edge ever waits in the queue;
- and yet `__scHandleRDP` finds it null.

None of the three statements is an assumption: each is measured.

### A display error in the probe, without consequence for the conclusion

The values recorded — `0x405F1280`, `0xB05F1280` — do not look like KSEG0 pointers,
which begin with `0x80`. Reversed, they give `0x80125F40`: my byte swap was
backwards in the display.

The "non-null" conclusion does not depend on the byte order and therefore holds. But
the printed value was wrong, and I only noticed on rereading. A probe that displays
an implausible value deserves to have the implausibility looked at before the result
is used — that is what saved the Z-against-W measurement, where an impossible
profile revealed a warm-up artefact.

### What must be measured next

The only way to settle it is to see the word change. A sampled watch on
`gMainSched + 0x278` at every guest thread switch would give the exact chronology of
its going to zero — that is more intrusive than anything done here, and it is now
the only open question.

## Caught in the act

The probe that was missing did not look at our paths but at **the game's instant**:
in `do_recv`, just after the scheduler's loop has taken a message out of its queue,
and just before it goes back into its handler.

    [trace][rcv] msg=667 curRSP=0x80111338 curRDP=0x00000000   (good=1)
    ...
    [trace][rcv] msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NULL
                                            (good=1157 nullSP=0 nullDP=1)

**A single occurrence in 1157 receptions**, and it is the log's last line — hence
the crash itself.

The detail that counts: **both fields are null**. Not only `curRDPTask`. The game no
longer has any task in progress, neither RSP nor RDP, when our DP edge arrives.

### What that changes

Every previous hypothesis was looking for why `curRDPTask` was cleared while a task
was in progress. The question is badly put: **the game is at rest**. Both handlers
have done their work, both fields are reset to zero, no task is waiting — and a DP
edge arrives all the same.

On the hardware, an RDP at rest signals nothing. Our edge is therefore one too many,
and the global count does not show it because there are **fewer** DP edges than
display lists: it is not a duplicate, it is a **late** edge.

The explanation consistent with everything measured: our graphics thread is
asynchronous. It takes a task, publishes SP, renders, then publishes DP. If the game
has meanwhile finished the task by another path — the SP edge is enough to make it
progress, and `__scHandleRetrace` can conclude it — then our DP arrives into the
void.

That also explains the rarity: the game must conclude the task before the rendering
finishes, which only happens on a particularly long frame.

### The measurement that remains

Compare, for that faulty DP edge precisely, the task it targeted with the game's
state. The labelling probe already exists and reported 300 concordances out of 300 —
but it measures **at the deposit**, and the faulty case occurs **at the reception**.
The message itself must therefore be labelled, or the expected value recorded at
deposit time to be read back at reception.

## The targeted task's state, and a fix placed in the wrong spot

The label carried through to the reception gives the complete picture of the faulty
case:

    msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NULL
    targeted=0x80125FB0 state=0x00000001 flags=0x00000023

`state = 1` is `OS_SC_NEEDS_RDP`: **the task is still waiting for the RDP**. It has
therefore not been concluded — the hypothesis of a late edge on an already-finished
task falls. The scheduler had simply **never granted it the RDP**: `__scExec` only
sets `curRDPTask` when the RSP task and the RDP task are the same, and when the RDP
is busy at scheduling time, the task starts on the RSP alone.

A fix attempted: defer the DP edge as long as `curRDPTask` does not designate our
task, reusing the retry queue.

**No effect.** And the reason reads in the measurements already made: at the
deposit, `curRDPTask` always designates the right task — three hundred times out of
three hundred. The deferral condition is therefore never true. The discrepancy is
born **between the deposit and the reception**, and a guard placed at the deposit
can see nothing of it.

It is the same placement error as the one made five times on the probes, transposed
to a fix: **acting where one observes, rather than where the phenomenon happens.**

The guard ought to be at the reception — but we have no hold on the moment the game
takes its message, short of altering its queue's semantics. Another shape remains to
be found: for instance publishing the DP edge only once the game has actually
granted the RDP, which implies waiting on the graphics thread's side rather than
depositing and deferring.

## The cause: a watchdog, not a scheduling problem

Twelve hypotheses eliminated, all about the **order** of the messages, all wrong —
because the defect is not an ordering defect. One had to stop reading our code and
read theirs.

`__scHandleRetrace`, in the decomp's `libultra/src/sc/sched.c`, carries a watchdog
that stock libultra does not have. Rare added it:

```c
if (sc->curRDPTask) gCurRDPTaskCounter++;

if ((gCurRDPTaskCounter > 10) && (sc->curRDPTask)) {
    if (sc->curRDPTask->unk68 == 0) {
        osSendMesg(sc->curRDPTask->msgQ, &gBootBlackoutMesg, OS_MESG_BLOCK);
    }
    set_curRDPTask_NULL = TRUE;
    gCurRDPTaskCounter = 0;
    osDpSetStatus(DPC_SET_XBUS_DMEM_DMA | DPC_CLR_FREEZE | DPC_CLR_FLUSH |
                  DPC_CLR_TMEM_CTR | DPC_CLR_PIPE_CTR | DPC_CLR_CMD_CTR);
}
...
if (set_curRDPTask_NULL) { sc->curRDPTask = NULL; }
```

Past ten retraces with an RDP task in progress, the game **declares the RDP hung**:
it resets `curRDPTask` to zero and reinitialises the DP registers — but it **does not
clear `OS_SC_NEEDS_RDP` on the task**. Our DP edge, arriving afterwards, walks into
`__scHandleRDP`, reads `curRDPTask == 0` there and dereferences zero.

It is word for word the state the probe had captured:

    msg=668 curRSP=0x00000000 curRDP=0x00000000 <== NULL
    targeted=0x80125FB0 state=0x00000001 flags=0x00000023

`curRDPTask` null **and** the targeted task still carrying `NEEDS_RDP`. Only two
paths set `curRDPTask` to zero, and `__scHandleRDP` clears `NEEDS_RDP` when it does.
The watchdog is the only one that leaves this combination behind. The measurement
had been pointing at the cause all along; it was its reading that was missing.

### The threshold is counted in retraces, not in milliseconds

The first draft of this diagnosis said: "ten retraces are 167 ms, and a software
Glide rendering on an emulated Pentium II exceeds that budget." The measurement
shipped with the fix **refutes it** — worst case **60 ms over more than a thousand
frames, zero overruns**.

The hypothesis was plausible, it explained the symptom, and the fix it inspired
works. Three reasons not to check it, and it is false all the same. That is
precisely the case where one would not have measured.

**With the reservation that counts, and that nearly went missing**: this build uses
the **null renderer**. The 60 ms are the display list's traversal alone, without a
single line rasterised. The measurement therefore refutes the hypothesis *for this
binary*, not for a real Glide rendering — which will cost considerably more and will
bring the budget question back. That is one more argument for publishing the edge
early: not a palliative, but the only structure that holds when the rendering gets
heavier. And 60 ms of mere traversal already produce six retraces per list; eleven
is not far, with nothing having been drawn.

What the delay costs is not time but **retraces handled in the meantime**:
`gCurRDPTaskCounter` is reset to zero by `__scExec` when a task starts, then
incremented once per `__scHandleRetrace`. Publishing the DP edge after `send_dl`
lets a whole render's worth of retraces slip in between.

The run's totals put a number on it: **3238 VIDEO messages against 567 RDP_DONE**,
that is about six retraces per display list. Comfortably under eleven on average —
and not under eleven in the tail of the distribution. One excursion is all it takes,
which explains why the crash struck **once**, at display list 344 of a run that was
otherwise progressing.

### The fix

Publish the DP edge **before** the rendering rather than after, exactly as the SP
edge already is — and for the same reason, written two lines higher in `events.cpp`:
the queued task owns an immutable snapshot of RDRAM, so the rendering never reads
memory the game might recycle, and the swap to the screen is ours, not the game's.

The fix carries with it the measurement that justifies it: `send_dl`'s duration,
worst case, and the count of frames beyond the 167 ms. Without it, the diagnosis
would be plausible instead of verifiable.

### Verified on the machine

Same ROM, same build otherwise:

| | display lists | presents | null receptions | crash dump |
|---|---|---|---|---|
| before | 344 then EXCEPTION | — | 1 | 2618 bytes |
| after | 1135 and climbing | 7020 | **0** | none (156 bytes) |

### Two observation traps cleared along the way

**A frozen program's log is zero bytes long.** Windows 95 only updates the size in
the directory at close time, and its write-behind cache holds the sectors. The
exception filter works around that by closing the stream before writing — but a
program that **hangs** reaches no handler. One then reads "zero bytes" and concludes
"the program produced nothing, so it stopped early", when it may be running
normally. `dkr_diag_commit` calls `_commit`, that is `FlushFileBuffers`, which forces
the cache **and** the directory entry; called now and then, it makes the log readable
in flight. That is what allowed the run to be read at 570 display lists and then at
1135 without interrupting it.

**`DKR_TRACE_SP` does not survive the Run box.** It is not in `autoexec.bat`, so a
launch from the Start menu gives an empty log — indistinguishable from a mute
program. The game is now launched through `D:\RUNDKR.BAT`, which sets the variable
before calling the executable.

**"Not responding" does not mean frozen.** Windows 95 displays that warning as soon
as a thread owning a window stops pumping its messages. The game runs in its own
thread and pumps nothing: the warning is therefore **normal**, and reading it as a
hang cost one more round trip. Three observations, three wrong readings, all of the
same kind: concluding from an absence of signal.

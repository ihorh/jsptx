# Reshaping the Pipeline

`docs/plucker.md` stage 2 shipped `jsptx .` and left the code in a shape the
maintainer rejected on 2026-09-10. This plans the way out, in steps small
enough to judge one at a time. It changes no scope in `plucker.md`, and sits
between that document's stage 2 and stage 3.

## What Is Wrong

Four complaints, each with the evidence in the tree.

**`process_block` serves two output channels.** It classifies, masks, trims,
traces, and dispatches a mode, which is why it takes eight parameters
(`src/jsp_run.c:79-81`). `.claude-notes/m3-abandoned.md` names the growing
parameter list as the tell that killed M3, where it reached nine. Nobody
counted this time.

**Every consumer walks the mask itself.** `jsp_pluck_step` decodes
`(block, mask)` into runs and structural bytes by hand (`src/jsp_pluck.c:136-149`).
A filter or a CSV structurer would each write that loop again.

**The plucker nests four deep.** `process_run` carries the byte walk and the
record state machine together, so its write calls sit four levels in
(`src/jsp_pluck.c:39-61`).

**Names say nothing.** `process_block`, `process_run`, `step_structural`, and
`discover_shape` name no domain concept between them.

**Headers and sources diverge.** Only `jsp_settings` matches its header.
`io.c`, `pluck.c`, `string_mask.c`, and `run.c` all differ from theirs.

## The Target Shape

Three stages, each a module, with trace as a log call rather than a stage.

| Stage | Owns | Emits |
|---|---|---|
| `jsp_reader` | `read`, refill, cutting fixed-width blocks | blocks |
| `jsp_scan` | classify, string mask, length trim, the ctz walk | tokens |
| the mode | records, paths, output bytes | writes to `out_fd` |

A token is a run of non-structural bytes, or one structural byte. Every
consumer wants both: the plucker copies runs and switches on structural bytes,
path matching compares key bytes from runs, and a filter buffers runs.

Trace sits between steps the way a log line does. A stage calls it where it
holds the number, and the destination is a handle built once in `jsp_run` from
`jsp_fds`. No token carries trace state, and no signature grows for it.

## The Steps

Each ends green, each is judged on its own, and each can be abandoned without
touching the ones before it.

| # | Step | Behavior change | Judged on |
|---|---|---|---|
| 0 | Close `pluck-identity` | none | — |
| 1 | One-to-one headers and sources | none | navigability |
| 2 | `jsp_reader` moves to its own module | none | `jsp_run.c` reads as a pipeline |
| 3 | ~~Trace becomes a module~~ | struck | see below |
| 4 | ~~Decide: tokens now, or stage 3 first~~ | resolved: tokens now | — |
| 5 | `jsp_scan` emits tokens | none | the loop reads as a flat verb list |
| 6 | The plucker's own shape | none | nesting and naming |

Steps 1 and 2 invent no abstraction. They move code that already exists into
files that already have names. Step 5 is the only one that adds a concept, and
step 4 exists so it gets decided with more information than today.

### Step 1 — One-to-One Headers and Sources (landed)

Landed on `rename-modules` as option (a): `jsp.h` became `jsp_run.h` and the
umbrella went. Seven sources and one header renamed, seven include lines and
one guard updated, nothing else touched. `zig build test` stayed green, and the
x86 cross-compile still builds the AVX2 path.

`src/jsp_X.c` implements `include/jsp_X.h`. `jzbuild` infers the file list from
the directories, so `build.zig` needed no edit.

Two exceptions stay, and both are named here rather than left as drift.
`main.c` is an entry point with no header. `jsp_classify.h` has three
implementations chosen by architecture, so `jsp_classify_scalar.c`,
`jsp_classify_neon.c`, and `jsp_classify_avx2.c` keep the one-to-three shape.

`jsp.h` forced the one real choice, since `main.c`, `bench/jsp_run_bench.c`,
and three tests included it. The alternatives, for the record:

- **(a) `jsp.h` becomes `jsp_run.h`, and the umbrella goes.** Seven include
  lines change. Taken: four headers are few enough to include directly.
  An umbrella built for a library nobody consumes is the speculative packaging
  this project deletes on sight.
- **(b) `jsp.h` splits.** `jsp_run.h` takes the types and the function,
  `jsp.h` keeps only includes. Zero churn at call sites, one new five-line
  file.
- **(c) `run.c` becomes `jsp.c`.** One rename, every other file untouched, and
  a name that hides what the file drives.

### Step 2 — `jsp_reader` Moves Out (landed, in five commits)

Grew from one step into five once the maintainer asked what `jsp_reader`
actually hides.

**Delete `JSP_PAD`.** M0 allocated it for a classifier still unwritten, and
every commit since has left those 64 octets untouched: every
design compacts the remainder to the front before padding, so the padding lands
inside `cap`. Verified by removing it and running the suite plus every input
length 0 to 200 at three buffer sizes under ASan and UBSan. `design.md`
justified it by alignment while stating that alignment needs nothing, and the
reader's comment justified it differently. Both went.

**`jsp_block` becomes `jsp_slice_u8`, joined by `jsp_buf_u8`.** The struct was
already a pointer and a length wearing a domain name. The `_u8` says the span
is octets, because this workspace will grow other slice types meaning other
things, and a bare `jsp_slice` claims more than it delivers. The rename also
unified the size type, since `len` was `unsigned` while the reader's counters
are `size_t`. `CHAR_BIT == 8` became a build failure, spelled two ways since CI
builds at c99 and c17.

**The reader moves to `jsp_reader.h`/`jsp_reader.c`.** `block_size` became an
init parameter, so the reader stops holding the classifier's vector width as a
constant.

**The reader stops filling the trailing block's tail.** Filling belongs to
whoever knows what the octets mean, and the reader knows neither what reads
them nor what would be inert to it. Nothing fills them now. The mask trim in
`jsp_run` has discarded those bits since M1, so seven filler bytes, an absent
fill, and a `0xAA`-poisoned buffer all produce identical output on all three
channels.
`calloc` replaces `malloc`, so no stage reads an indeterminate octet — that was
the only thing filling ever bought.

**`jsp_buf_u8_tail` returns a buffer rather than a pointer**, with
`jsp_buf_u8_room` folded into it. One call instead of two, so a pointer from one
buffer cannot be paired with a length from another.

### Step 3 — Trace Becomes a Module (struck, 2026-09-10)

**Do not re-add this.** It said to move `emit_offsets`, `emit_mask`, and
`emit_trace` into `jsp_trace.h`/`jsp_trace.c`, and it was in the plan for a bad
reason: it dropped `process_block` from eight parameters to six, back when the
parameter count was being read as the defect rather than the signal.

Three things kill it. Step 5 deletes `process_block`, which is trace's only
call site, so relocating first decides where the calls go twice. No second
consumer exists, and `tests/run_test.c` already drives trace through `jsp_run`'s
descriptors rather than through the emit functions. And
`.claude-notes/observability-and-io.md` holds the trace layer unsettled until a
real use case shows where the boundaries fall — step 5 is that use case, so
settling it first is the guess that note warns against.

Step 5 absorbs it. Once tokens exist, `--offsets` is a call where the token is
and `--masks` is a call where the mask is, which is what trace-as-logging means.

### Step 4 — The Decision Point (resolved, 2026-09-10)

The question was whether `jsp_scan` should wait for `plucker.md` stage 3, so
that path matching exists as a second caller before the token stream is shaped
around the plucker alone.

It collapses under the rule in "Too Late, Too Early". A `jsp_token` carrying a
run of octets or one structural octet makes `jsp_scan` an iterator over set bits
in a mask: a primitive, and a primitive earns its place on reading better rather
than on a second caller. `jsp_slice_u8` landed on the same argument.

**The condition, and it binds.** The token stays BYTES and STRUCTURAL. A kind
naming something the program decides — `JSP_TOKEN_KEY`, `JSP_TOKEN_RECORD_START`
— moves `jsp_token` out of the primitive row, and the two-caller bar returns.

So: step 5 now, stage 3 after it.

### Step 5 — `jsp_scan` Emits Tokens (landed)

`process_block` and `process_finish` disappear. `jsp_run`'s loop pulls tokens,
traces, and pushes. The plucker's bit walk (`src/jsp_pluck.c:136-149`) deletes.

Splittable in two, if the whole move reads as too large to judge:

- **5a.** `jsp_scan` exists and emits `(block, mask)`, moving classify, mask,
  and trim out of `process_block`. The plucker keeps its bit walk.
- **5b.** `jsp_scan` emits tokens, and the bit walk moves into it.

Alternative to the whole step: **keep `(block, mask)` and inline
`process_block` into `jsp_run`'s loop.** The loop becomes a flat list of five
verbs with no new concept at all. It costs every future consumer its own bit
walk. This is the smallest possible answer to the complaint, and it is real.

On modes, once tokens exist: `jsp_run` switches on `settings.output` once and
each mode runs its own loop, declaring exactly the state it needs. A third mode
adds a loop rather than a parameter. The alternative is one tagged sink struct
with a switch per token, which keeps one loop and pays an indirection.

As landed, neither: one loop, with `plucking` gating the push. Two modes pay
for neither shape, so a third mode is the point to pick one.

### Step 6 — The Plucker's Own Shape

Four levels of nesting and four weak names, addressed after the token stream
settles what the plucker receives. Deliberately last, since its input decides
its shape.

## Too Late, Too Early

A count is a signal to look. It is never the verdict.

**What a count should prompt is a trajectory question.** `process_block` at
eight parameters would be fine if eight were the ceiling. It is wrong because
the next mode and the next channel each add one, and nothing bounds that.
`jsp_scan_next(&scan)` takes one parameter today and still takes one after a
third mode lands. That difference decides it, and the number only pointed at
it.

Nesting reads the same way. Two loops over blocks and then bytes follow the
data's own shape, so the depth is honest. Four nested `if`s inside
`process_run` follow from one function doing two jobs, so the depth is a
symptom.

**Vocabulary divides by what it hides.** A primitive hiding a standard
algorithm at near-zero cost earns its place as soon as it reads better —
`jstr`, a slice, an iterator. An abstraction hiding what the program decides
has to earn it against a higher bar, because the reader loses the decision.

| Hides | Here | Needs |
|---|---|---|
| a standard algorithm | `jsp_reader`, iterating set bits, `jsp_trace`'s formatting | that it reads better |
| what the program decides | `process_block`'s mode dispatch, the plucker's phase machine | callers whose shapes differed |

**This relaxes step 4.** `jsp_token` as proposed carries a run of bytes, or one
structural byte. That makes `jsp_scan` an iterator over set bits in a mask: a
header-only primitive hiding a standard algorithm, and it needs no second
caller.

The caller count returns the moment a token kind names something the program
decides. `JSP_TOKEN_RECORD_START` or `JSP_TOKEN_KEY` would move `jsp_token` out
of the primitive row and into the second one, and then two callers is the bar
again. Keeping the token dumb is what keeps step 5 cheap.

## What Decides Better or Worse

These check that a step landed. None of them is a reason to take one.

| Measure | At the start | Now | Target |
|---|---|---|---|
| `process_block` parameters | 8 | gone | gone by step 5 |
| `jsp_run.c` lines | 231 | 62 | — |
| Deepest nesting in `jsp_pluck.c` | 4 | 4 | 2 by step 6 |
| Files whose `.c` matches its `.h` | 1 of 5 | 8 of 8 | held |
| Consumers writing a mask walk | 1 | 0 | 0 by step 5 |

`zig build test` stays green at every step, and the fixtures agree at
`--buf-size=64` and `--buf-size=4096`, carrying M1's cross-buffer-size
criterion forward.

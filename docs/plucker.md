# The Plucker

`jsptx` beats `jq` on usability. Performance is a non-functional requirement,
comparable or faster, rather than the goal itself.

This document plans the first command a user would actually run. It replaces
no decision in `design.md`, which records what the architecture settled on.
`brainstorm.md` holds the wider vision these milestones serve one slice of.

## Why This Comes First

Everything shipped so far serves the performance requirement. The SIMD
classifier turns a byte stream into a structural index, and a benchmark scores
it in ns per block. The usability thesis stays untested, because the tool emits
byte offsets rather than anything a user asked for.

`brainstorm.md:90-92` names the use case:

> extract a nested value (for example `$.user.id`) from a massive array of
> objects and print it to stdout line by line.

`brainstorm.md` asks whether the filter or the plucker comes first, at `:206`
and again at `:341`, and answers neither time. The plucker wins on cost. It
emits the moment a path matches, so it holds no record in memory. The filter
must buffer one, because the deciding field can arrive last. The path tracking
built here is what the filter and the CSV structurer each need afterwards.

## What Ships

```console
$ cat events.ndjson | jsptx .user.id
[42,71]
$ cat events.ndjson | jsptx --lines .user.id
42
71
```

One value per record, in constant memory, at a throughput that stands beside
`jq`'s.

## The Stages

Four branches, each landing on its own. Every stage ends green, and ends with
something runnable, so the work survives being picked up cold.

1. **`docs-refocus`** — correct `design.md`. No code.
2. **`pluck-identity`** — `jsptx .`, printing each record verbatim.
3. **`pluck-path`** — `jsptx .user.id`.
4. **`pluck-bench`** — a throughput number beside `jq`'s.

Stage 1 stands alone and can land at any point. Stage 2 carries the real work,
proving the streaming machinery with no path logic in it. That leaves stage 3
as key matching on top. `git branch` reports what has landed.

## Scope

**In.** A dot path of literal keys, and `.` alone for the whole record.
Newline-delimited, concatenated, and top-level-array input. Values reach stdout
**verbatim**, carrying the input's own bytes, quotes and escapes untouched,
wrapped in one JSON array. `--lines` writes them one per line instead. String,
number, literal, object, and array values all emit.

**Out, each with the trigger that would bring it in.** Every deferral waits on
a user hitting the limitation.

| Deferred | Trigger |
|---|---|
| `--path-sep=CHAR` | A key containing `.` proves unaddressable |
| `--no-unwrap` | Someone needs a top-level array as one value |
| Unescaping, and a `-r` flag | Piping a string into `grep` hurts enough to ask |
| Array indices, wildcards, comparisons | `brainstorm.md`'s use cases 1 and 3 |
| Number parsing | Something needs a number's value |
| `--newline=space\|strip\|error` under `--lines -r` | Someone's strings legitimately hold newlines and erroring costs more than mangling |
| `--by-document`, grouping output per input document | Someone needs to know which document a value came from |

The debug and trace layer stays unsettled on purpose. This work should show
where the code's boundaries actually fall, and settling the layer first would
guess at them.

## Semantics

### Records

`jsptx` works one record at a time, and finds records two ways. A stream of
JSON values makes each value a record, whether newline-delimited or simply
concatenated. A top-level array makes each of its elements a record.

The rule is local rather than one-shot: **a `[` arriving at depth 0 is a
wrapper, and anything else at depth 0 is a record.** So a stream holding several
documents treats each the same way, wherever it sits.

| Input on stdin | `jsptx .` yields |
|---|---|
| `[1,2][3,4]` | `[1,2,3,4]` |
| `{"a":1}[1,2]` | `[{"a":1},1,2]` |
| `[1,2]{"a":1}` | `[1,2,{"a":1}]` |

Document boundaries reach the output nowhere, which is the point. Framing is
what jsptx abstracts away, so `{"id":1}`⏎`{"id":2}` and `[{"id":1},{"id":2}]`
stay interchangeable. `--by-document` is the fix if someone needs to know which
document produced what.

| Input on stdin | Records are | `jsptx .id` prints | `jq` needs |
|---|---|---|---|
| `{"id":1}`⏎`{"id":2}` | the two objects | `[1,2]` | `jq -r '.id'` |
| `[{"id":1},{"id":2}]` | the two objects | `[1,2]` | `jq -r '.[].id'` |

One command covers both shapes, where `jq` needs a different expression for
each. That is the usability edge. The array case is required rather than
optional, since `brainstorm.md:90-92` names an array of objects as the input.

The cost is that a whole top-level array has no address. `echo '[1,2,3]' |
jsptx .` prints three values rather than one. Nothing turns that off today.
`--no-unwrap` is the fix when someone wants it.

A record lacking the path stays silent, so the output holds hits rather than
records. `jq` prints `null` in that position. Duplicate keys emit twice, which
follows from carrying no dedup state.

### Keys with Spaces or Special Characters

The path splits on `.` and on nothing else, so every other byte in a segment
stands for itself. `jsptx '.user name'` matches the key `user name`, and the
quoting belongs to the shell rather than to the parser. The same holds for
`-`, `/`, `@`, and any UTF-8.

One limitation remains. A key containing `.` has no address, because `.a.b`
reads as two segments. `--path-sep=CHAR` costs about five lines and composes
with any key lacking that one byte. A quoted segment in `jq`'s style, `."a.b"`,
is the fallback if a key defeats every separator.

Matching compares bytes, so `"b"` fails to match segment `b`. Fixing that
needs the unescaping this milestone defers.

### A Path Landing on a Container

The container emits verbatim, and raises no error. Three reasons:

- `jq -r '.user'` behaves the same way, so nothing here surprises.
- `jsptx .` and `jsptx .user | jsptx .id` both compose.
- Refusing costs more code than emitting. The depth counter that finds a
  container's closing bracket already runs for position tracking. Emission is
  therefore free, where refusal adds kind detection and an error path.

A path running through a scalar needs no handling of its own. Given `.user.id`
where `user` holds `42`, segment `id` never matches, and the record stays
silent.

## Design

### Nothing Is Buffered

No key or value is ever copied, which lets a span exceed the read buffer
harmlessly. This holds the constant-memory requirement, and it is the part
worth getting right.

`jsp_reader` (`src/jsp_reader.c`) compacts its remainder to the front of the
buffer and reads in behind it, so every pointer into that buffer dies on
refill. All new state is
therefore counters.

**Key matching keeps no key.** The walk compares the key's bytes against the
segment it is matching, as blocks stream past. It carries an index and a
still-matching flag. At the closing quote the key matched when the flag holds
and the index reached the segment's length.

**Value emission keeps no value.** A match sets an emitting flag, and bytes go
straight to `out_fd` as each block is walked.

**A value's end depends on its kind**, decided at the first non-whitespace byte
after the `:`. Whitespace is not structural (`src/jsp_structural_chars.h:11-18`),
so ending at the next structural character would emit `{"a": 1 , ...}` as `1 `,
carrying a trailing space.

| Kind | Ends at |
|---|---|
| String | The closing `"`, already in the filtered mask |
| Object or array | Depth returning to its entry level |
| Bare scalar | The first whitespace or structural byte |

A JSON number or literal can contain neither whitespace nor a structural byte,
which is what makes the last row safe.

**Position** comes from `jsp_depth_state` for container kind and depth, plus a
counter of contiguously matched leading segments. The walk attempts a key match
only at a depth equal to that counter, and decrements on popping above it.

**The unwrap decision is derived, never latched.** `jsp_depth_state` pushes one
bit per open container, `1` for an object and `0` for an array, so the outermost
container is bit `depth - 1`. Records therefore sit one level in exactly when
that bit is clear:

```c
record_depth = (depth > 0 && ((stack >> (depth - 1)) & 1) == 0) ? 1 : 0;
```

That retires `shape_known`, `unwrap`, and `record_depth` from
`jsp_pluck_state`. Stage 2 latched the shape on the stream's first
non-whitespace byte, which made the first top-level array unwrap and every
later one emit as a record.

### Nothing Is Synthesized

The plucker writes only framing of its own: `[`, `,`, and `]` around the
values, or the `\n` between them under `--lines`. Every other
byte it writes comes from the input, copied from the block it arrived in. So a
value reaches stdout with its escapes, its Unicode, its interior whitespace,
its duplicate keys, and its `1.0000000000000001` exactly as written, because
nothing in the tool looks inside a value or re-encodes one.

The line to hold: **content bytes are never synthesized, framing bytes may
be.** The array's brackets and the newline under `--lines` are framing, and
framing stays outside every value.

This is what makes two deferred features expensive rather than small. `-r` must
decode `\u00e9` and emit UTF-8, and pretty-printing must parse a value's
structure and write it back out. Both synthesize content, which costs a
decoder, an encoder, and UTF-8 validation the tool currently does without.

### Output Is One JSON Array

JSON goes in and JSON comes out, whatever the path lands on. A scalar keeps its
quotes, so `jsptx .` on a string record yields `["hello"]` rather than `hello`.
That holds for every kind of value, which is what lets `jsptx .user | jsptx .id`
compose, and what keeps the shape stable when a path moves from a scalar to a
container.

The array is streamed, never buffered: `[`, then each value as it is found,
separated by `,`, then `]`. Memory stays constant.

**The cost is truncation, and it is accepted rather than solved.** jsptx exists
for multi-gigabyte streams. A run that dies at four gigabytes leaves an
unterminated document, so everything already written fails to parse. A sequence
of newline-separated values would have left four gigabytes of usable data. That
is the price of one predictable output shape, and `--lines` is the way out for a
caller who would rather have the prefix.

### `--lines`, and the One Rule It Keeps

`--lines` drops the array framing and writes one value per line. It holds a
single promise, and enforces it: **anything that would break one value per line
is an error.**

Two things break it, and the rule covers both with no special case.

| Input | Why it breaks the promise |
|---|---|
| a container value | it carries the input's own newlines, so it spans lines |
| `-r` on a string holding `\n` | unescaping puts a newline in the content |

Precisely: the run errors when the octets it would emit contain `0x0A` or
`0x0D`, naming the offset. A tab leaves line structure intact, so the rule
passes it through rather than failing ordinary extracted text over it.

The promise then holds absolutely. A JSON number or literal is newline-free by
its grammar, and RFC 8259 requires a string to escape every character below
0x20. A scalar's own octets are therefore always exactly one line.

Raw output stays opt-in for its own reason. `-r` prints a string unquoted while
a container stays JSON, and that is the one setting where the output's shape
depends on what the path found.

### Three Hazards

Each one is verified against the code rather than assumed.

1. **A stream ending on a 64-byte boundary yields no final block.**
   `jsp_reader_next` returns `JSP_READER_END` with the buffer empty. A value
   still emitting must terminate after the loop breaks, or the last value loses
   its newline in silence.
2. **The byte walk bounds by `block.len`, never by 64.** `jsp_reader_next`
   pads the trailing block past `len` with the filler byte `jsp_run` gave it. `process_block` clears those
   bits from the mask (`src/jsp_run.c:80-82`), and a byte walk between mask bits
   has no equivalent guard, so it would emit pad spaces.
3. **`jsp_string_state.in_string` is an end-of-block value.**
   `include/jsp_string_mask.h:18-30` updates it once per block, which makes it
   wrong mid-block. Derive position inside a string from the quote bits the
   walk already steps over.

## The Work, by Stage

Each step names the goal it serves, since infra serves a functional,
performance, or maintainability goal, or it waits.

### Stage 1 — `docs-refocus` (maintainability)

`design.md:16-17` states the goal as throughput and memory. That framing is
what licensed M3, so correct it: the usability thesis leads, and performance is
the requirement beside it.

Retire the milestone plan. M0 through M2 have shipped, and `git log` plus the
per-milestone notes describe them more accurately. Delete the M3 spec, so it
misleads nobody again. Keep "Decisions That Bind" and "Deferred, with Reasons".
Those hold reasoning and measurements no reader can recover from the code.

### Stage 2 — `pluck-identity` (functional)

Ships `jsptx .`, printing each record's bytes verbatim, one per line. It stands
on its own as a streaming record splitter, and it exercises every hard part of
the design with no path logic involved.

1. `git cherry-pick 13b5c62` from `m3-depth-framing` brings
   `include/jsp_depth.h` and `tests/depth_test.c`, and nothing besides. That
   commit stands alone. The file earns its place here, since the plucker needs
   record boundaries, container kind, and detection of malformed nesting.
2. The byte walk steps the ranges between mask bits, bounded by `block.len`. A
   new `jsp_pluck_state` threads through `process_block` the way
   `jsp_string_state` does at `src/jsp_run.c:120`.
3. Value-kind dispatch and write-through emission reuse `write_all`
   (`jsp_write_all`). A value in flight terminates when the reader reports
   `JSP_READER_END`.
4. The CLI takes a positional path argument in `src/jsp_settings.c`, which
   already hand-parses flags and uses `jstr` (`include/jstr.h`). `--masks`,
   `--sink`, and `--buf-size` stay as the debugging and benchmarking surface.
5. Fixtures land under `tests/data/pluck/`, following the `tests/data/strings/`
   pattern `tests/strings_test.c` established, run at `buf_size` 64 and 4096.

### Stage 3 — `pluck-path` (functional)

Ships `jsptx .user.id`, adding path splitting, incremental key matching, and
the matched-segment counter. Stage 2 built everything else.

It also fixes record discovery, decided 2026-09-10: every top-level array
unwraps, not only the first, and the decision reads the depth stack rather than
a latched flag.

It also changes the output format, decided the same day:
the default becomes one JSON array, and `--lines` opts back into newline
separation. The two ship together, since the default alone would leave a caller
piping into `while read` with no way back. That rewrites seven golden fixtures
under `tests/data/pluck/` and fifteen assertions in `tests/pluck_test.c`, which
is why it rides along with a stage already touching them.

Three fixtures matter most, each drawn from attacking the no-buffer claim:

- a bare scalar value at EOF, landing exactly on a 64-byte boundary,
- a key longer than the whole read buffer,
- an object value spanning a refill, holding nested `{}` and an escaped `\"`.

### Stage 4 — `pluck-bench` (performance)

The requirement makes this mandatory rather than optional. `bench/` already
feeds `zig build bench` through `build.zig`, so extend it to time the same
pluck over the same input and report MB/s beside `jq`'s.

This machine's absolute numbers swing by half on battery, so measure on AC
power and compare only within a run.

## Verification

- `zig build test` stays green, the shipped suites included.
- `printf '{"user":{"id":42}}\n{"user":{"id":71}}\n' | jsptx .user.id` prints
  `[42,71]`, and with `--lines`, `42` then `71`.
- `echo '[{"user":{"id":1}},{"user":{"id":2}}]' | jsptx .user.id` prints
  `[1,2]`, unwrapping the array with no `.[]`.
- `echo '{"user":{"id":1}}' | jsptx .user` prints `[{"id":1}]`.
- `printf '{"user name":7}' | jsptx '.user name'` prints `[7]`.
- `echo '{"a":{"b":1}}' | jsptx --lines .a` exits non-zero, naming the offset:
  a container breaks one value per line.
- Every fixture agrees at `--buf-size=64` and `--buf-size=4096`, carrying M1's
  cross-buffer-size criterion forward.
- Resident memory stays flat against a multi-gigabyte stream.
- `zig build bench` reports a throughput number beside `jq`'s.

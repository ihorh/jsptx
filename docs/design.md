# jsptx — Design

`jsptx` finds the structure in a JSON byte stream using SIMD, one 64-byte block
at a time. That pass now feeds a user-facing command: `jsptx .` splits a stream
into records and prints each verbatim. The structural offsets it finds remain
available as a trace, under `--offsets`.

This document records what the design settled on and why. `docs/brainstorm.md`
holds the earlier exploration that led here, including options this document
rejects. `docs/plucker.md` plans the first user-facing command on top of what
is settled here.

The tool was called `jxs` in that brainstorm. It is `jsptx` everywhere now.

## What jsptx Is, and Is Not

`jsptx` beats `jq` on usability. That is the thesis this project tests.
Performance is a non-functional requirement beside it, comparable or faster
than `jq` rather than the goal itself.

A streaming JSON tool needs a parser, and the parser starts with the pass
that finds structure. `git log` and the per-milestone notes under
`.claude-notes/` describe how that pass got built more accurately than a plan
written before the work started would. The classifier reaches from a raw
byte stream to structural offsets; reading numbers, unescaping strings, and
building a tree all sit past that boundary, and so does the expression
language and the output writer.

## Decisions That Bind

### Standard Input Only

`jsptx` reads file descriptor 0 and takes no path argument. A path argument
invites `mmap`, and `mmap` hands the program an aligned, padded, whole buffer
for free. That deletes the streaming problem this project exists to solve.

Streaming does not beat the operating system. Reading with `read(2)` instead of
`fread` saves one copy, which is noise next to the SIMD win. The reason to own
the buffer is that the SIMD pass needs padding and a controlled tail, and stdio
provides neither.

ISO C's `fread` also blocks until it fills the whole buffer. Ask for 256 KB and
it waits for 256 KB or for end of file, so a slow writer stalls the tool.
`read(2)` returns what has arrived.

### The Block Is 64 Bytes on Every Architecture

AVX2 registers hold 32 bytes and NEON registers hold 16. The program still steps
64 bytes at a time on both. The mask is therefore always a `uint64_t`, and the
block boundary always falls in the same place.

That confines the architecture-specific code to one function:

```c
uint64_t jsp_classify64(const uint8_t *p);
```

x86-64 implements it with two AVX2 loads and two `_mm256_movemask_epi8` calls,
combined into 64 bits. AArch64 implements it with four NEON loads and four mask
extractions. A scalar version implements it for every other target.

Everything downstream is portable C99: the tail mask, the bit iteration through
`__builtin_ctzll`, the offset arithmetic, and the refill.

### The Offset Stream Is Stable, the Mask Is Not

Pass one's output is an index. Both text streams below observe that index, and
neither is the product's output. Pass two consumes the index in memory, so
nothing downstream ever parses these lines.

Default output is one line per structural character:

```text
<byte offset>\t<character>
```

The offset is absolute in the stream. A mask is not. It says where a character
sits inside its block, and the block boundary moves with the read size. Piping the same file in 100-byte writes changes every mask and changes no
offset.

Two acceptance criteria depend on that stability. The same input must produce
identical output on Apple silicon and on x86-64, and it must produce identical
output at any buffer size. Only the offsets can carry a golden.

`--masks` prints the raw hex masks anyway, because reading them is how a person
debugs the classifier.

### Three Portability Tiers

Pure ANSI C is off the table, because the classifier includes `<immintrin.h>` or
`<arm_neon.h>`, and both headers belong to the compiler rather than to a
standard. Given that, the rule is where each tier may appear.

| Tier | What it covers | Where it lives |
| --- | --- | --- |
| ISO C99 | Buffers, masks, offsets, state, flags | Everywhere |
| POSIX | `read`, `write`, and `pipe` in tests | One file |
| Compiler intrinsics | `jsp_classify64` | One file per architecture |

POSIX buys exactly one thing worth having, which is `read(2)` on a descriptor.
Alignment needs nothing, because both architectures load unaligned at full
speed, so plain `malloc` replaces `posix_memalign` and C11's `aligned_alloc`. Flag parsing is a dozen lines by hand, so `getopt`
stays out.

Under `-std=c99` the POSIX declarations are hidden. The I/O file defines
`_POSIX_C_SOURCE 200809L` ahead of its includes, or `read` arrives undeclared.

### Nesting Is Bounded, and Recursion Is Banned

A JSON parser needs one bit per nesting level, recording whether the current
container is an object or an array. An explicit array of bits holds that as
well as the call stack does.

Recursive descent on a million open brackets overflows the C stack and crashes,
reporting nothing. An explicit stack that grows on demand converts that crash
into unbounded memory growth from hostile input. A fixed bound converts it into
an error at the exact byte, found with no lookahead. It also holds the parser's
memory constant for every input. RFC 8259 §9 permits an implementation to limit
nesting depth.

At a bound of 64 or less the whole stack is one `uint64_t`. Push is
`stack = (stack << 1) | is_object`, pop is `stack >>= 1`, and the current
container is `stack & 1`.

### Tests Are C, Driven Through Descriptors

The pipeline entry point takes descriptors and a buffer size:

```c
int jsp_run(int in_fd, int out_fd, size_t buf_size);
```

A test creates a `pipe()`, writes the input in whatever chunk sizes it chooses,
runs the pipeline, and compares what comes back. Short reads are the property
that matters most in the read loop, and a file on disk cannot produce one. A
pipe can.

`jzbuild` already builds one binary per `tests/*_test.c` behind `zig build
test`, so this needs no build wiring. Shell tests against the installed binary
may follow later, once the command-line surface is worth testing as a surface.

### An Eventuality Binds, an Ephemeral Thing Expires

This document states where the design lands. A claim about the end state binds.
Scaffolding that stands in for an unbuilt part expires instead, and a later
milestone retires it.

Building on something ephemeral is fine while its nature stays in view. The
tests are the working example. They assert against text that pass two will never
read, and they earn their keep until pass one exposes its index as data.

The failure is citing an expired claim as settled. Before a decision in this
section closes an argument, ask whether it survives the next milestone.

## Deferred, with Reasons

- **`mmap`.** It is genuinely faster for files and it is the right benchmark
  ceiling. It is out of the milestones because it removes the tail and the
  refill, which are the work.
- **Zig `@Vector`.** It solves the portable comparison and leaves the mask
  extraction, which is the hard half. The project is C, and one function is a
  small enough seam to hand-write.
- **Clang vector extensions.** Same limit as Zig's vectors, for the same reason:
  no portable movemask.
- **A lookup table in the scalar classifier.** A 256-entry table answers "is
  this byte structural" with one load. It measured about five times the
  comparison chain's throughput on Apple silicon, and a 65536-entry table
  reading two bytes at a time doubled that again. Both are out because the
  scalar path serves targets without SIMD, and those cores carry a small L1
  cache. A 64 KB table is slowest on exactly the hardware it exists for. The
  comparison chain also stays because it is the test oracle.
- **SWAR in the scalar classifier.** Comparing 8 bytes at a time through a
  64-bit register measured slower than the 256-entry table and read worse than
  either alternative. Clang also auto-vectorized it into NEON, so the number it
  produced described the compiler rather than the technique.
- **Pseudo-structural characters.** A real second pass needs the first byte of
  every scalar value, not only the structural characters. It is cheap to add and
  nothing consumes it yet.
- **Everything past pass one.** The value filter, the plucker, the CSV writer,
  the expression language, and the TUI all live in `docs/brainstorm.md`.

## Open Questions

- Whether the test step should build with a sanitizer, and which one.

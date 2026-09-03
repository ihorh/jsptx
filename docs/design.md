# jsptx — Design and Milestones

`jsptx` finds the structure in a JSON byte stream using SIMD, one 64-byte block
at a time. It reads standard input, and it emits the byte offset of every
character that carries structure.

This document records what the design settled on and why, and it splits the
work into four milestones. Each milestone is a branch. `docs/brainstorm.md`
holds the earlier exploration that led here, including options this document
rejects.

The tool was called `jxs` in that brainstorm. It is `jsptx` everywhere now.

## What jsptx Is, and Is Not

The end goal is a streaming JSON tool that beats `jq` on throughput and on
memory. That tool needs a parser, and the parser starts with the pass that finds
structure. These four milestones build that pass and stop there.

These milestones stop at offsets. Reading numbers, unescaping strings, and
building a tree all sit past that boundary, and so do the expression language
and the output writer.

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

### The Index Stream Is the Contract, the Mask Is a Detail

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
speed, so plain `malloc` with an over-allocated pad replaces `posix_memalign`
and C11's `aligned_alloc`. Flag parsing is a dozen lines by hand, so `getopt`
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

## The Milestones

### M0 — Echo

Read standard input and write it to standard output, byte for byte. There is no
JSON in this milestone. It exists because the read loop, the write loop, and the
test harness are the floor everything else stands on.

The work:

- A single buffer allocation, its capacity a multiple of 64, plus 64 bytes of
  padding that later milestones fill.
- A read loop that treats a return of 0 as end of file, retries on `EINTR`, and
  treats a short read as ordinary rather than as the end.
- A write loop, because `write(2)` on a pipe also returns short and must be
  resumed.
- `--buf-size=N`, which the tests use to force the loop into its edge cases.

Accepted when the echo is byte-identical across four inputs: empty, one byte,
larger than the buffer, and delivered in one-byte pipe writes.

Block consumption and the carried remainder arrive in M1, because M0 has nothing
that consumes a block.

### M1 — Structural Index

Classify `{`, `}`, `[`, `]`, `:`, `,`, and `"` in 64-byte blocks, and emit the
absolute offset of each one.

The work:

- `jsp_classify64` in three implementations: AVX2, NEON, and scalar.
- The scalar version is also the test oracle. On random input the SIMD result
  must equal the scalar result, which is the cheapest real confidence available
  and it costs one test.
- Block consumption, then a `memmove` of the sub-block remainder to offset 0,
  then a read in behind it.
- The end-of-file tail, the one partial block per run. Pad it with `0x20`,
  classify it, then clear the mask bits past the real length.
- Bit iteration with `__builtin_ctzll`, and the `<offset>\t<char>` output.
- `--masks`.

Accepted when three comparisons match. arm64 must equal x86-64. Buffer sizes 64,
65, 4096, and 1 MiB must all agree. The scalar and SIMD classifiers must agree
on random bytes.

M1 is wrong on purpose for any structural character inside a string, so
`{"url":"http://x{y}"}` reports braces that carry no structure. Those fixtures
live in `tests/data/strings/`, M1 does not run them, and M2 must pass them.

An invalid document such as `{"a":}` produces a correct index stream, because
this pass validates nothing. An invalid fixture at M1 asserts only that the
program survives it and reports the right offsets.

### M2 — String Mask

Turn off structural recognition inside strings, and make the `strings/`
fixtures pass.

The work:

- The backslash mask, and the run-start parity that decides whether a quote is
  real or escaped.
- A prefix XOR over the real-quote mask, which yields the in-string mask. Six
  shift-and-XOR steps on a `uint64_t` compute it, so the carry-less multiply
  instruction is an optimization rather than a requirement.
- The two bits that cross a block boundary, and cross a refill with it: still
  inside a string, and still inside a backslash run.
- The structural mask becomes `structural & ~in_string`.
- A sink mode that classifies and discards, because one line per structural
  character emits more bytes than it reads, and a throughput number measured
  through `printf` measures `printf`.
- A benchmark, and then the nibble shuffle table. Seven compares and six ORs per
  block become roughly four operations on both architectures, through
  `_mm256_shuffle_epi8` and `vqtbl1q_u8`. The table lands with a measured
  before and after, or it does not land.

Accepted when the `strings/` fixtures pass, the cross-architecture and
cross-buffer-size criteria from M1 still hold, and the benchmark reports a
number from the sink mode.

### M3 — Depth and Framing

A scalar pass over the index stream that tracks nesting and finds record
boundaries.

The work:

- The bit stack, `--max-depth` with a default of 64, and a word array above 64.
- An error at the exact byte for a depth overrun, for a mismatched close
  bracket, and for a close bracket arriving on an empty stack.
- Depth returning to zero, which marks one complete top-level value. That is the
  record boundary a streaming filter needs, and it is what makes concatenated
  and newline-delimited JSON work without a special case.

Accepted when nesting errors report the right byte, and when a stream of
concatenated records reports the right boundaries.

Still not validated here: numbers, literals, key uniqueness, UTF-8, and any
grammar rule beyond bracket matching.

## Deferred, with Reasons

- **`mmap`.** It is genuinely faster for files and it is the right benchmark
  ceiling. It is out of the milestones because it removes the tail and the
  refill, which are the work.
- **Zig `@Vector`.** It solves the portable comparison and leaves the mask
  extraction, which is the hard half. The project is C, and one function is a
  small enough seam to hand-write.
- **Clang vector extensions.** Same limit as Zig's vectors, for the same reason:
  no portable movemask.
- **Pseudo-structural characters.** A real second pass needs the first byte of
  every scalar value, not only the structural characters. It is cheap to add and
  nothing consumes it yet.
- **Everything past pass one.** The value filter, the plucker, the CSV writer,
  the expression language, and the TUI all live in `docs/brainstorm.md`.

## Open Questions

- Whether the test step should build with a sanitizer, and which one.
- Whether `.claude-notes/` exists in this repository, and whether git tracks it.

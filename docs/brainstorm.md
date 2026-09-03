# jxs: a Json Stream Console Parser Better Than jq

Notes from a design conversation held on 31 August 2026 about a conceptual tool,
`jxs` (json Streamer), meant to outperform `jq` for streaming JSON on the
console.

## Problems With `jq` to Solve

- A steep, obtuse domain-specific language (DSL).
- High memory overhead on massive files.
- Crashing or stalling on malformed or incomplete stream chunks.

## Core Architecture and Improvements Over `jq`

- **SIMD-accelerated parsing**: use simdjson principles, or Rust's `simd-json`,
  to parse raw bytes at gigabyte-per-second speeds, bypassing `jq`'s
  sequential bottleneck.
- **Familiar scripting (JavaScript/expr) over a DSL**: instead of `jq`'s
  cryptic syntax, use standard embedded JavaScript (via QuickJS or V8) or a
  clean, familiar expression language such as SQL or JMESPath, mirroring
  tools like `fx`.
- **Tolerant stream recovery**: automatically recover from
  newline-delimited, fragmented, or trailing incomplete JSON chunks without
  crashing the pipe.
- **O(1) memory streaming**: process infinite streams by evaluating elements
  lazily as tokens arrive, rather than slurping input or building heavy ASTs
  in memory.

## Key Features of `jxs`

- **Intuitive chaining**: standard dot notation and array methods (`.map`,
  `.filter`) used natively.
- **Live interactive and pipe dual mode**: default to a high-throughput CLI
  pipe, but press a hotkey (such as Ctrl+F or Tab) to instantly fork into an
  interactive terminal UI (TUI) for digging through the live stream tree.
- **Grep-friendly flat output (like `gron`)**: an optional mode that flattens
  paths (`data.users.0.name = "Alice"`) so standard Unix tools like `grep`,
  `sed`, and `awk` work seamlessly out of the box.

## Proposed Syntax Comparison

`jq` (cryptic DSL):

```bash
cat stream.json | jq -c 'select(.level == "error") | .message'
```

`jxs` (JavaScript/expr engine):

```bash
cat stream.json | jxs "x => x.level === 'error' ? x.message : null"
```

## Open Questions From the Initial Proposal

- Which underlying language runtime to use: Rust, Go, or QuickJS.
- What memory buffer strategy to use for handling broken network streams.
- How to design the TUI interface controls.


## Follow-Up: Language Choice and DIY SIMD in C

User's question (31 August 2026, lightly cleaned up):

> I'd start with a few very practical use cases instead of solving a generic
> problem. Also not sure — I like C more, but it seems Rust is a great choice
> for building this tool. I'm also thinking about Zig. How hard is it to
> implement a SIMD parser on my own in C?

### C, Rust, or Zig for a Custom SIMD Parser

Building a SIMD JSON parser from scratch in C is highly challenging but
deeply rewarding. It requires a strong grasp of bitwise operations, CPU
intrinsics (AVX2/NEON), and state-machine design.

For a custom stream parser, Zig or C are actually better choices than Rust
if you want absolute control over memory layout and zero-allocation
streaming. Rust's `simd-json` is excellent, but it heavily relies on its type
system and ownership model, which can add abstraction overhead when dealing
with raw, fragmented byte streams.

### Step 1: Three Practical Streaming Use Cases

Instead of a generic parser, design specifically for these three
hyper-practical operations:

1. **The structural filter (drop noise)**: pass through only the JSON
   objects where `level == "error"`, instantly discarding megabytes of debug
   logs without allocating memory for them.
2. **The plucker (extract value)**: extract a nested value (for example
   `$.user.id`) from a massive array of objects and print it to stdout
   line by line.
3. **The structurer (convert to CSV)**: pluck three specific keys
   (`timestamp`, `status`, `ip`) and output them as raw tab-separated values
   for immediate Unix pipe chaining.

### How SIMD JSON Parsing Works: the Two-Pass Architecture

To implement this in C, you cannot use traditional character-by-character
loops. You must use the two-pass architecture popularized by the simdjson
paper:

```text
[ Pass 1: Structural Indexing ]  --> Scans 32/64 bytes at once using SIMD.
                                     Finds structural characters: {, }, [, ], :, ,, ", \
                                     Outputs a bitmask/array of structural positions.

[ Pass 2: Token Navigation ]     --> Skips all whitespace and internal string data.
                                     Jumps directly to the structural indexes.
                                     Evaluates your filter/pluck expression.
```

### Implementing Pass 1 in C (AVX2 Example)

A conceptual, simplified implementation of Pass 1 in C using Intel AVX2
intrinsics. It processes 32 bytes at a time in a single CPU instruction,
identifying where structural JSON tokens start.

```c
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>

// Identifies structural characters in a 32-byte chunk
void identify_structural_chars(const uint8_t *block, uint32_t *out_mask) {
    // Load 32 bytes of raw JSON stream into a 256-bit vector
    __m256i data = _mm256_loadu_si256((const __m256i*)block);

    // Create comparison vectors for target JSON structural characters
    __m256i brace_open  = _mm256_set1_epi8('{');
    __m256i brace_close = _mm256_set1_epi8('}');
    __m256i colon       = _mm256_set1_epi8(':');
    __m256i comma       = _mm256_set1_epi8(',');
    __m256i quote       = _mm256_set1_epi8('"');

    // Compare our data vector against all targets simultaneously
    __m256i cmp_bo  = _mm256_cmpeq_epi8(data, brace_open);
    __m256i cmp_bc  = _mm256_cmpeq_epi8(data, brace_close);
    __m256i cmp_col = _mm256_cmpeq_epi8(data, colon);
    __m256i cmp_com = _mm256_cmpeq_epi8(data, comma);
    __m256i cmp_q   = _mm256_cmpeq_epi8(data, quote);

    // Combine all match vectors using bitwise OR
    __m256i structural = _mm256_or_si256(
        _mm256_or_si256(cmp_bo, cmp_bc),
        _mm256_or_si256(cmp_col, _mm256_or_si256(cmp_com, cmp_q))
    );

    // Extract the internal 8-bit match results into a single 32-bit integer bitmask
    // Each '1' bit represents the exact index of a structural character in the 32-byte block
    *out_mask = (uint32_t)_mm256_movemask_epi8(structural);
}

int main() {
    // 32 bytes of a sample JSON log line
    const uint8_t chunk[32] = "{\"level\":\"error\",\"msg\":\"failed\"}";
    uint32_t mask = 0;

    identify_structural_chars(chunk, &mask);

    printf("JSON Chunk: %s\n", chunk);
    printf("SIMD Structural Bitmask (Hex): 0x%08X\n", mask);
    // You can now use __builtin_ctz (count trailing zeros) to rapidly step through the '1' bits
    return 0;
}
```

### The Reality Check: Four Hardest Hurdles in C or Zig

If you build this yourself, Pass 1 is the easy part. The actual complexity
lies in handling the edges:

- **The string quote problem**: a colon `:` inside a string
  (`"url": "http://foo"`) is not a structural element. Your SIMD pass must
  compute a "string mask" to toggle structural recognition off while inside
  quotes. Handling escaped quotes (`\"`) inside SIMD is notoriously tricky.
- **Buffer alignments**: SIMD vectors require data to be read in strict 32-
  or 64-byte blocks. If your network socket or file read cuts off mid-chunk,
  you must handle the manual padding of the remaining bytes without reading
  out of bounds.
- **Cross-platform vectorization**: if you write raw AVX2 intrinsics
  (`_mm256_*`), your code will crash on ARM (Mac M-series, AWS Graviton). You
  will need to write a fallback ARM NEON block using `vld1q_u8`, or use
  Zig's native `@Vector` syntax, which auto-compiles to the host
  architecture's SIMD.
- **State machine tracking**: to perform "the plucker" use case efficiently,
  Pass 2 must maintain a tiny, fast stack of keys (for example matching
  `user` then `id`) as it steps through the SIMD structural indexes.

### Why Zig Is an Incredible Middle Ground

If you love C, Zig gives you the exact same memory layout power but solves
the SIMD portability nightmare. In Zig, you don't need CPU-specific
intrinsics. You can write:

```zig
const bytes: @Vector(32, u8) = stream_buffer[0..32].*;
const matches = bytes == @as(@Vector(32, u8), @splat('{'));
```

The Zig compiler will automatically compile that into AVX2 instructions on
Intel, NEON on ARM, or a fast unrolled loop if the CPU doesn't have SIMD.

### Open Questions From This Follow-Up

- Implement use case 1 (the structural filter) or use case 2 (the plucker)
  first?
- Target portable Zig vector code, or stick to pure C/AVX2 for maximum
  bare-metal transparency?

## Follow-Up: Mac M-Series Compatibility

User's question (31 August 2026):

> What about a version compatible also with Mac M series?

To make a custom SIMD JSON parser compatible with Apple M-series chips
(ARM64 architecture) alongside Intel/AMD (x86_64), there are two distinct
architectural paths depending on whether you choose C or Zig. Apple Silicon
uses ARM NEON (128-bit vectors) instead of Intel's AVX2 (256-bit vectors).

### Strategy 1: the C Route (Architecture-Specific Branches)

In pure C, you must write two separate implementations using compiler
macros to detect the processor type at compile time. You trade off 32-byte
chunks on Intel for 16-byte chunks on ARM, processing two chunks
back-to-back on ARM to keep up.

```c
#include <stdint.h>
#include <stdio.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define PLATFORM_ARM 1
#elif defined(__AVX2__)
    #include <immintrin.h>
    #define PLATFORM_X86 1
#endif

// Multi-platform structural token identifier
uint32_t identify_structural_chars_portable(const uint8_t *block) {
    uint32_t mask = 0;

    #if defined(PLATFORM_X86)
        // Intel/AMD AVX2 Path: Processes 32 bytes at once
        __m256i data = _mm256_loadu_si256((const __m256i*)block);
        __m256i brace_open = _mm256_set1_epi8('{');
        __m256i brace_close = _mm256_set1_epi8('}');

        __m256i cmp = _mm256_or_si256(_mm256_cmpeq_epi8(data, brace_open),
                                      _mm256_cmpeq_epi8(data, brace_close));
        mask = (uint32_t)_mm256_movemask_epi8(cmp);

    #elif defined(PLATFORM_ARM)
        // Apple Silicon NEON Path: Processes 16 bytes at once
        uint8x16_t data = vld1q_u8(block);
        uint8x16_t brace_open = vdupq_n_u8('{');
        uint8x16_t brace_close = vdupq_n_u8('}');

        uint8x16_t cmp = vorrq_u8(vceqq_u8(data, brace_open),
                                  vceqq_u8(data, brace_close));

        // Extracting bitmask on ARM NEON requires narrowing down bytes to bits
        // We use a common shift-and-add trick to combine the results into a 16-bit integer
        static const uint8_t __attribute__((aligned(16))) shift_data[16] = {
            1, 2, 4, 8, 16, 32, 64, 128,
            1, 2, 4, 8, 16, 32, 64, 128
        };
        uint8x16_t shift_mask = vld1q_u8(shift_data);
        uint8x16_t masked = vandq_u8(cmp, shift_mask);

        // Pairwise addition down to a single 16-bit value
        uint16x8_t paired = vpaddlq_u8(masked);
        uint32x4_t quad = vpaddlq_u16(paired);
        uint64x2_t dual = vpaddlq_u32(quad);

        uint64_t low = vgetq_lane_u64(dual, 0);
        uint64_t high = vgetq_lane_u64(dual, 1);
        mask = (uint32_t)(low | (high << 8));
    #else
        // Fallback standard scalar loop if no SIMD is present
        for(int i = 0; i < 16; i++) {
            if (block[i] == '{' || block[i] == '}') mask |= (1 << i);
        }
    #endif

    return mask;
}
```

### Strategy 2: the Zig Route (Hardware Agnostic)

If you use Zig, you do not need to deal with platform macros, `arm_neon.h`,
or `immintrin.h`. Zig features an explicit built-in `@Vector` type. The
standard compiler target evaluates the system architecture and emits the
correct hardware assembly (AVX2 instructions on Intel Mac/Linux, NEON
instructions on M1/M2/M3 Apple devices) seamlessly.

```zig
const std = @import("std");

pub fn identifyStructural(block: *const [16]u8) u16 {
    // Treat the byte array as a hardware SIMD Vector
    const data: @Vector(16, u8) = block.*;

    // Create broad comparison vectors
    const b_open: @Vector(16, u8) = @splat('{');
    const b_close: @Vector(16, u8) = @splat('}');

    // Perform SIMD vector comparisons across all elements concurrently
    const match_open = (data == b_open);
    const match_close = (data == b_close);

    // Vector OR compiles directly to 'vorrq_u8' on ARM and '_mm_or_si128' on x86
    const combined_matches = match_open | match_close;

    // Pack the boolean vector results cleanly into a 16-bit scalar integer mask
    return @bitCast(combined_matches);
}
```

### Performance Differences: Mac M-Series vs. Intel

When running this code on an M-series Mac:

- **The latency advantage**: Apple Firestorm/Avalanche performance cores
  feature massive instruction decode widths (up to 8 instructions per
  cycle). The token navigation phase (Pass 2) will actually execute
  significantly faster than on most standard Intel CPUs.
- **The width limitation**: because ARM NEON tops out at 16 bytes per vector
  (128-bit) compared to AVX2's 32 bytes (256-bit), you will step through the
  input stream file in increments of 16 bytes instead of 32 bytes per
  iteration.

### Open Questions From This Follow-Up

- Prefer the Zig approach to avoid platform maintenance overhead, or the
  explicit C path to deeply understand how ARM and Intel handle SIMD
  registers differently?
- Focus on use case 1 (the structural filter) to isolate the target objects
  next?

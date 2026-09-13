# jsptx

Find the structure in a JSON byte stream with SIMD, 64 bytes at a time.

`jsptx` reads a JSON stream on standard input and prints the values a path
selects, each one carrying the input's own bytes untouched. It never buffers a
record, so a multi-gigabyte stream costs the same memory as a small one.

```bash
$ printf '{"user":{"id":42}}\n{"user":{"id":71}}\n' | jsptx .
{"user":{"id":42}}
{"user":{"id":71}}
```

## Status

Early, and useful for one thing: `jsptx .` splits a stream into records and
prints each verbatim. Newline-delimited, concatenated, and top-level-array
input all work, and `--buf-size` forces the read loop's edge cases.

Paths beyond `.` are next, so `jsptx .user.id` does not work yet.
`docs/plucker.md` plans that stage and the two output shapes it brings.

CI builds and tests on Linux at every push, and on macOS daily or on request,
at both `c17` and `c99`.

## Building

Zig 0.16 or later, and nothing else. `zig build` fetches the one dependency on
first run.

```bash
zig build          # build, and install to zig-out/bin
zig build run      # build and run
zig build test     # build and run every test binary
zig build bench    # throughput, in nanoseconds per 64-byte block
```

The design targets x86-64 and arm64, on macOS and on Linux. The classifier has
a NEON path, an AVX2 path, and a scalar path that serves everything else.

## Documentation

| File | What it holds |
| ---- | ------------- |
| `docs/plucker.md` | The live plan: what ships in each stage, and the semantics it commits to |
| `docs/design.md` | The decisions that bind, with their reasons |
| `docs/brainstorm.md` | The exploration that led there, including the options `design.md` rejects |
| `docs/c-style.md` | C conventions beyond `.clang-format` and `CLAUDE.md` |
| `docs/pipeline.md` | A closed plan, kept for the reasoning behind the module split |

## AI-Assisted Development

Claude assists on this project, across design and code alike. `docs/design.md`
records the decisions, their reasons, and the alternatives they reject. The
reasoning lives there rather than in commit trailers.

## License

MIT. See `LICENSE`.

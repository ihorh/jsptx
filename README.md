# jsptx

Find the structure in a JSON byte stream with SIMD, 64 bytes at a time.

`jsptx` reads standard input and reports the byte offset of every character
that carries structure. It is the first pass of a streaming JSON parser. That
pass finds where things sit, and a later one decides what they mean.

## Status

Early. M0 (echo) is done: `jsptx` reads standard input and writes it to
standard output byte for byte, over a single padded buffer, with `--buf-size`
to force the read loop's edge cases. M1 through M3 have yet to start. CI builds
and tests on Linux (every push) and macOS (daily, or on request) at both `c17`
and `c99`.

## Building

Zig 0.16 or later, and nothing else. `zig build` fetches the one dependency on
first run.

```bash
zig build          # build, and install to zig-out/bin
zig build run      # build and run
zig build test     # build and run every test binary
```

The design targets x86-64 and arm64, on macOS and on Linux.

## Documentation

| File                 | What it holds                                       |
| -------------------- | --------------------------------------------------- |
| `docs/design.md`     | The decisions that bind, with reasons, and the milestone map |
| `docs/brainstorm.md` | The exploration that led there, including the options `design.md` rejects |
| `docs/c-style.md`    | C conventions beyond `.clang-format` and `CLAUDE.md` |

## AI-Assisted Development

Claude assists on this project, across design and code alike. `docs/design.md`
records the decisions, their reasons, and the alternatives they reject. The
reasoning lives there rather than in commit trailers.

## License

MIT. See `LICENSE`.

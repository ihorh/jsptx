# jsptx

Find the structure in a JSON byte stream with SIMD, 64 bytes at a time.

`jsptx` reads standard input and reports the byte offset of every character
that carries structure. It is the first pass of a streaming JSON parser. That
pass finds where things sit, and a later one decides what they mean.

## Status

Early. The binary prints a greeting and nothing else. The design and the
milestone map are written, and the first milestone has yet to start.

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

## AI-Assisted Development

Claude assists on this project, across design and code alike. `docs/design.md`
records the decisions, their reasons, and the alternatives they reject. The
reasoning lives there rather than in commit trailers.

## License

MIT. See `LICENSE`.

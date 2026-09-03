# jsptx — C Style

Conventions beyond what `.clang-format` and `CLAUDE.md` already cover. Add a
rule here once it comes up twice, not before.

- **Every `if`/`for`/`while` body is braced, single statement or not.**
  `.clang-format` does not enforce this — `InsertBraces` only inserts braces
  when reformatting, so it wouldn't catch a bare body that slips in. A second
  look at review does. Settled following the same rule in jcraft's
  `docs/c-style.md`.

# jsptx — C Style

Conventions beyond what `.clang-format` and `CLAUDE.md` already cover. Add a
rule here once it comes up twice, not before.

- **The slice and buffer vocabulary matches jcraft's `jstr`, name for name and
  contract for contract.** `_first(s, n)` clamps, because "at most n" is a
  request a caller makes on purpose. `_after(s, n)` asserts, because consuming
  more octets than exist means the caller lost track. `_empty(s)` reports
  rather than aborts. A check should never be the thing that kills the
  program. This repository vendors `include/jstr.h`, so the two vocabularies
  sit side by side and a shared name has to carry a shared contract. This rule
  lands on first use rather than second. The family is meant to move into a
  shared library later, and a drifted contract would not survive that move.

- **Every `if`/`for`/`while` body is braced, single statement or not.**
  `.clang-format` does not enforce this — `InsertBraces` only inserts braces
  when reformatting, so it wouldn't catch a bare body that slips in. A second
  look at review does. Settled following the same rule in jcraft's
  `docs/c-style.md`.

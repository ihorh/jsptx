# Reshaping the Pipeline — Closed 2026-09-12

`docs/plucker.md` stage 2 shipped `jsptx .` and left the code in a shape the
maintainer rejected on 2026-09-10. This planned the way out, in six steps small
enough to judge one at a time, and it changed no scope in `plucker.md`.

It is finished. The plan itself is kept for one reason: the rules it settled for
where a boundary belongs, in "Too Late, Too Early" below. The step-by-step
account is in `git log` between `pluck-identity` and the `reader-slices` merge.

## What Was Wrong

- **`process_block` served two output channels**, which is why it took eight
  parameters. `.claude-notes/m3-abandoned.md` names the growing parameter list
  as the tell that killed M3, where it reached nine.
- **Every consumer walked the mask itself.** A filter or a CSV structurer would
  each have written that loop again.
- **The plucker nested four deep**, because one function carried the byte walk
  and the record state machine together.
- **Names said nothing.** `process_block`, `process_run`, `step_structural`,
  and `discover_shape` named no domain concept between them.
- **Headers and sources diverged.** Only `jsp_settings` matched its header.

## The Steps, as Resolved

| # | Step | Outcome |
|---|---|---|
| 1 | One-to-one headers and sources | landed |
| 2 | `jsp_reader` moves to its own module | landed, five commits |
| 3 | Trace becomes a module | **struck** — trace is a log call, and a log call needs no module |
| 4 | Decide: tokens now, or stage 3 first | resolved: tokens first |
| 5 | `jsp_scan` emits tokens | landed |
| 6 | The plucker's own shape | landed |

The target it aimed at, and reached: `jsp_reader` owns reading and cuts blocks,
`jsp_scan` owns classification and emits tokens, and the mode owns records and
output. A token is a run of non-structural bytes, or one structural byte.

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

## What It Measured

These check that a step landed. None of them is a reason to take one.

| Measure | At the start | Now | Target |
|---|---|---|---|
| `process_block` parameters | 8 | gone | gone by step 5 |
| `jsp_run.c` lines | 231 | 64 | — |
| Deepest nesting in `jsp_pluck.c` | 4 | 2 | 2 by step 6 |
| Files whose `.c` matches its `.h` | 1 of 5 | 8 of 8 | held |
| Consumers writing a mask walk | 1 | 0 | 0 by step 5 |

`zig build test` stays green at every step, and the fixtures agree at
`--buf-size=64` and `--buf-size=4096`, carrying M1's cross-buffer-size
criterion forward.

## As Closed

The plan is done, and the branch went past it in four ways, each judged on its
own at the time:

- `jsp_bits` exists as a module, holding `trailing_zeros`, `low_mask`,
  `run_parity`, and `prefix_xor`, with its own test binary.
- `jsp_string_mask` is deleted. Its escape and string passes live in
  `structural_mask` inside `src/jsp_scan.c`, which takes the cross-block carry
  as an in-out parameter.
- `jsp_slice_u8` gained `first` and `empty` beside `after`, matching jcraft's
  `jstr` contracts. `docs/c-style.md` records why those contracts stay
  identical across repositories.
- The plucker writes the newline before each record, and `jsp_pluck_finish` is
  gone.

Stage 3 in `docs/plucker.md` inherits one thing from here. It is the
`jsp_pluck_shape` enum, and that stage replaces it with the depth stack.

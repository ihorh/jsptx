#ifndef JSP_H
#define JSP_H

#include <stddef.h>

/* jsp_run's real output, to out_fd. */
typedef enum {
    JSP_OUTPUT_SINK,  /* classifies (and plucks, if traced) and discards; out_fd is never touched */
    JSP_OUTPUT_PLUCK, /* one line per record: its own bytes verbatim, newline-terminated */
} jsp_output_mode;

/* jsp_run's inspection stream, to trace_fd, independent of mode: a run can
   pluck to out_fd and trace the classification behind it to trace_fd at
   the same time. All text output is observability, not the product; see
   docs/design.md's "The Offset Stream Is Stable, the Mask Is Not". */
typedef enum {
    JSP_TRACE_NONE,
    JSP_TRACE_OFFSETS, /* one line per structural character: "<offset>\t<char>\n" */
    JSP_TRACE_MASKS,   /* one line per block: "<offset>\t<mask>\n" */
} jsp_trace_mode;

/* Reads in_fd until end of file, classifying the structural JSON characters
   { } [ ] : , " in 64-byte blocks.

   mode decides what reaches out_fd. JSP_OUTPUT_SINK writes nothing at all:
   every block is still classified, but the result is discarded rather than
   formatted and written, for measuring the pipeline's own cost apart from
   its I/O. JSP_OUTPUT_PLUCK writes each record's own bytes verbatim, one
   per line: a record is a value in a concatenated or newline-delimited
   stream, or an element of a single top-level array, which is unwrapped
   rather than emitted itself. See docs/plucker.md.

   trace decides what reaches trace_fd, the same way and at the same time,
   whatever mode is doing with out_fd. JSP_TRACE_NONE writes nothing.
   JSP_TRACE_OFFSETS writes one line per structural character found:
   "<offset>\t<char>\n", offset absolute in the input stream. JSP_TRACE_MASKS
   writes one line per block instead: "<offset>\t<mask>\n", mask the block's
   64-bit classification as 16 lowercase hex digits and offset the block's
   first byte.

   Rounds buf_size up to the nearest multiple of 64, with a minimum of 64,
   and reads in chunks of that size. Returns 0 on success, -1 on a read or
   write error with errno set by the failing call, or, with JSP_OUTPUT_PLUCK,
   on malformed input: unbalanced or mismatched brackets, or nesting past
   JSP_MAX_DEPTH. */
int jsp_run(int in_fd, int out_fd, size_t buf_size, jsp_output_mode mode, int trace_fd,
            jsp_trace_mode trace);

#endif /* JSP_H */

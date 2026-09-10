#ifndef JSP_RUN_H
#define JSP_RUN_H

#include "jsp_trace.h"
#include "jstr.h"

#include <stddef.h>

/* jsp_run's real output, to out_fd. */
typedef enum {
    JSP_OUTPUT_SINK, /* classifies (and plucks, if traced) and discards; out_fd is never touched
                      */
    JSP_OUTPUT_PLUCK, /* one line per record: its own bytes verbatim, newline-terminated */
} jsp_output_mode;

/* Where jsp_run reads, writes, and, eventually, reports: err_fd is not yet
   used by jsp_run itself. trace_fd is meaningful only when settings.trace
   below is not JSP_TRACE_NONE. */
typedef struct {
    int in_fd;
    int out_fd;
    int err_fd;
    int trace_fd;
} jsp_fds;

/* Everything jsp_run does once it's reading, cli-supplied or defaulted
   alike. path is meaningful only when output is JSP_OUTPUT_PLUCK; it views
   the matching argv entry, which outlives the process. trace is orthogonal
   to output: either can be set with the other, tracing the classification
   behind whatever output is doing. */
typedef struct {
    size_t          buf_size;
    jsp_output_mode output;
    jsp_trace_mode  trace;
    jstr            path;
} jsp_settings;

typedef enum {
    JSP_OK = 0,
    JSP_ERR_ALLOC,
    JSP_ERR_IO,
    JSP_ERR_BLOCK_PROCESS_TMP,
} jsp_status;

typedef struct {
    jsp_status status;
    int        sys_errno;
} jsp_result;

/* Reads fds.in_fd until end of file, classifying the structural JSON
   characters { } [ ] : , " in 64-byte blocks.

   settings.output decides what reaches fds.out_fd. JSP_OUTPUT_SINK writes
   nothing at all: every block is still classified, but the result is
   discarded rather than formatted and written, for measuring the
   pipeline's own cost apart from its I/O. JSP_OUTPUT_PLUCK writes each
   record's own bytes verbatim, one per line: a record is a value in a
   concatenated or newline-delimited stream, or an element of a single
   top-level array, which is unwrapped rather than emitted itself. See
   docs/plucker.md.

   settings.trace decides what reaches fds.trace_fd, the same way and at
   the same time, whatever output is doing with out_fd. JSP_TRACE_NONE
   writes nothing. JSP_TRACE_OFFSETS writes one line per structural
   character found: "<offset>\t<char>\n", offset absolute in the input
   stream. JSP_TRACE_MASKS writes one line per block instead:
   "<offset>\t<mask>\n", mask the block's 64-bit classification as 16
   lowercase hex digits and offset the block's first byte.

   Rounds settings.buf_size up to the nearest multiple of 64, with a
   minimum of 64, and reads in chunks of that size. Returns 0 on success,
   -1 on a read or write error with errno set by the failing call, or,
   with JSP_OUTPUT_PLUCK, on malformed input: unbalanced or mismatched
   brackets, or nesting past JSP_MAX_DEPTH. */
jsp_result jsp_run(jsp_fds fds, jsp_settings settings);

#endif /* JSP_RUN_H */

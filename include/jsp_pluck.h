#ifndef JSP_PLUCK_H
#define JSP_PLUCK_H

#include "jsp_nesting.h"
#include "jsp_scan.h"

#include <stdint.h>

/* Where the walk sits relative to the record it is currently between or
   inside. A record is a top-level value, except that every top-level array
   unwraps and its elements are the records instead. BETWEEN covers
   whitespace, the separators of an unwrapped array, and that array's own
   brackets. A record is found the moment BETWEEN sees its first byte, and
   the record's own kind decides which phase follows. */
typedef enum {
    JSP_PLUCK_BETWEEN,
    JSP_PLUCK_STRING,    /* a string record, open quote already emitted */
    JSP_PLUCK_CONTAINER, /* an object or array record, tracked via depth */
    JSP_PLUCK_SCALAR,    /* a bare number, true, false, or null record */
} jsp_pluck_phase;

/* Carried across every block of a stream, one call per block, in order.
   Zero-initialize for the first block: phase starts BETWEEN and nesting empty,
   matching jsp_nesting_state's own zero-initialization contract. */
typedef struct {
    jsp_pluck_phase phase;
    _Bool record_seen; /* whether a record has started; every later one gets a newline first */
    jsp_nesting_state nesting;
} jsp_pluck_state;

/* Takes one token, writing each record's own bytes verbatim straight to
   out_fd, with a newline before every record but the first; jsp_run writes
   the last one. No key path is applied: jsptx . prints every record
   whole. Call once per token from jsp_scan_next, in order, for every
   BYTES and STRUCTURAL the stream yields. Returns 0, or -1 on a write error or
   on malformed input: unbalanced or mismatched brackets, or nesting past
   JSP_MAX_DEPTH. */
int jsp_pluck_push(jsp_pluck_state *state, jsp_token token, int out_fd);

#endif /* JSP_PLUCK_H */

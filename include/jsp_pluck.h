#ifndef JSP_PLUCK_H
#define JSP_PLUCK_H

#include "jsp_depth.h"
#include "jsp_scan.h"

#include <stdint.h>

/* Where the walk sits relative to the record it is currently between or
   inside. BETWEEN covers whitespace, the separators of an unwrapped array,
   and that array's own brackets. A record is found the moment BETWEEN sees
   its first byte, and the record's own kind decides which phase follows. */
typedef enum {
    JSP_PLUCK_BETWEEN,
    JSP_PLUCK_STRING,    /* a top-level string record, open quote already emitted */
    JSP_PLUCK_CONTAINER, /* a top-level object or array record, tracked via depth */
    JSP_PLUCK_SCALAR,    /* a top-level bare number, true, false, or null */
} jsp_pluck_phase;

/* Where the records sit, latched from the stream's first non-whitespace
   byte. */
typedef enum {
    JSP_PLUCK_SHAPE_UNKNOWN, /* no non-whitespace byte seen yet */
    JSP_PLUCK_SHAPE_ARRAY,   /* one top-level array; its elements are the records */
    JSP_PLUCK_SHAPE_VALUES,  /* the top-level values are the records */
} jsp_pluck_shape;

/* Carried across every block of a stream, one call per block, in order.
   Zero-initialize for the first block: phase starts BETWEEN, the shape
   UNKNOWN, and depth empty, matching jsp_depth_state's own
   zero-initialization contract. */
typedef struct {
    jsp_pluck_phase phase;
    jsp_pluck_shape shape;
    _Bool record_seen; /* whether a record has started; every later one gets a newline first */
    jsp_depth_state depth;
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

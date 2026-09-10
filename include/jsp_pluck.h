#ifndef JSP_PLUCK_H
#define JSP_PLUCK_H

#include "jsp_depth.h"
#include "jsp_slice_u8.h"

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

/* Carried across every block of a stream, one call per block, in order.
   Zero-initialize for the first block: phase starts BETWEEN, the shape is
   undiscovered, and depth starts empty, matching jsp_depth_state's own
   zero-initialization contract. */
typedef struct {
    jsp_pluck_phase phase;
    _Bool    shape_known; /* whether the stream's first non-whitespace byte has been seen */
    _Bool    unwrap; /* the whole stream is one top-level array; its elements are records */
    unsigned record_depth; /* the depth records sit at: 0, or 1 once unwrap */
    jsp_depth_state depth;
} jsp_pluck_state;

/* Walks one block, writing each record's own bytes verbatim, terminated by
   a newline, straight to out_fd, with no key path applied: jsptx . prints
   every record whole. mask is jsp_filter_structural_mask's result for the
   same block, trimmed to block.len the way process_block already trims it
   for the offset stream. Returns 0, or -1 on a write error or on malformed
   input: unbalanced or mismatched brackets, or nesting past JSP_MAX_DEPTH. */
int jsp_pluck_step(jsp_pluck_state *state, jsp_slice_u8 block, uint64_t mask, int out_fd);

/* Closes a bare scalar record left in flight when the stream ends exactly
   where it stands: jsp_pluck_step only ever closes a scalar on a following
   whitespace or structural byte, and end of input supplies neither. A
   no-op in every other phase, since a string or container record always
   closes synchronously on its own terminating byte. Call once, after the
   last call to jsp_pluck_step for a stream. Returns 0, or -1 on a write
   error. */
int jsp_pluck_finish(jsp_pluck_state *state, int out_fd);

#endif /* JSP_PLUCK_H */

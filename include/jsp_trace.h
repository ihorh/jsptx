#ifndef JSP_TRACE_H
#define JSP_TRACE_H

#include "jsp_slice_u8.h"

#include <stdint.h>

/* The inspection stream, independent of whatever a run is producing on its own
   output. All text output is observability, not the product; see
   docs/design.md's "The Offset Stream Is Stable, the Mask Is Not". */
typedef enum {
    JSP_TRACE_NONE,
    JSP_TRACE_OFFSETS, /* one line per structural character: "<offset>\t<char>\n" */
    JSP_TRACE_MASKS,   /* one line per block: "<offset>\t<mask>\n" */
} jsp_trace_mode;

/* fd matters only when mode is something other than JSP_TRACE_NONE. Passed by
   value, since it is two words and never mutated. */
typedef struct {
    int            fd;
    jsp_trace_mode mode;
} jsp_trace;

static inline jsp_trace jsp_trace_make(int fd, jsp_trace_mode mode) {
    return (jsp_trace){fd, mode};
}

/* Reports one classified block, whichever mode asked for. JSP_TRACE_MASKS
   writes a line for every block, an all-zero mask included, since that
   symmetry is what makes the mode useful for finding a quiet block.
   JSP_TRACE_OFFSETS writes a line per set bit instead, walking the mask
   itself: the walk is duplicated from jsp_scan rather than shared, so one
   call at one level covers every mode and the hot path keeps no trace branch.

   offset locates block.ptr[0] in the stream, and mask is bit i for
   block.ptr[i]. Returns 0, or -1 on a write error with errno set. */
int jsp_trace_block(jsp_trace t, uint64_t offset, jsp_slice_u8 block, uint64_t mask);

#endif /* JSP_TRACE_H */

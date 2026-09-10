#include "jsp_pluck.h"

#include "jsp_io.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool is_json_ws(uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* The stream's shape is decided by its very first non-whitespace byte,
   wherever it turns up: a mask bit in jsp_pluck_step, or a bare scalar's
   lead byte found scanning a run in process_run. A '[' there means the
   whole stream is one top-level array to unwrap, and its elements, one
   depth in, are the records; anything else means depth 0 holds them. */
static void discover_shape(jsp_pluck_state *state, uint8_t c) {
    state->shape_known = true;
    state->unwrap = c == '[';
    state->record_depth = state->unwrap ? 1 : 0;
}

/* Handles bytes[start,end): a run with no structural byte in it, since
   those are all mask bits and jsp_pluck_step routes them elsewhere. A
   string or container record's content needs no byte-level look at this
   run at all, since only the record's own terminating mask bit can end it,
   so the whole run is emitted verbatim in one write. Between records, and
   inside a bare scalar, the run is where the transition actually happens:
   whitespace ends a scalar, and a non-whitespace byte outside a bracket or
   quote starts one, so this scans byte by byte to find it. */
static int process_run(jsp_pluck_state *state, const uint8_t *bytes, size_t start, size_t end,
                       int out_fd) {
    if (end <= start) {
        return 0;
    }
    if (state->phase == JSP_PLUCK_STRING || state->phase == JSP_PLUCK_CONTAINER) {
        return jsp_write_all(out_fd, bytes + start, end - start);
    }

    size_t span_start = start; /* meaningful only once phase is JSP_PLUCK_SCALAR */
    for (size_t i = start; i < end; i++) {
        bool ws = is_json_ws(bytes[i]);
        if (state->phase == JSP_PLUCK_SCALAR) {
            if (ws) {
                if (jsp_write_all(out_fd, bytes + span_start, i - span_start) != 0) {
                    return -1;
                }
                if (jsp_write_all(out_fd, (const unsigned char *)"\n", 1) != 0) {
                    return -1;
                }
                state->phase = JSP_PLUCK_BETWEEN;
            }
            continue;
        }
        /* JSP_PLUCK_BETWEEN */
        if (!ws) {
            if (!state->shape_known) {
                discover_shape(state, bytes[i]); /* never '[': that is always a mask bit */
            }
            state->phase = JSP_PLUCK_SCALAR;
            span_start = i;
        }
    }
    if (state->phase == JSP_PLUCK_SCALAR) {
        return jsp_write_all(out_fd, bytes + span_start, end - span_start);
    }
    return 0;
}

/* Handles one mask bit: one of { } [ ] : , " in stream order, exactly the
   set jsp_depth_step expects. Every one of them passes through depth
   tracking once, whatever phase it arrives in. */
static int step_structural(jsp_pluck_state *state, uint8_t c, int out_fd) {
    bool opening_wrapper = false;
    if (!state->shape_known) {
        discover_shape(state, c);
        opening_wrapper =
            state->unwrap; /* true here means c == '[' by discover_shape's own test */
    }

    jsp_depth_result r = jsp_depth_step(&state->depth, c);
    if (r == JSP_DEPTH_ERROR_OVERFLOW || r == JSP_DEPTH_ERROR_MISMATCH ||
        r == JSP_DEPTH_ERROR_UNBALANCED) {
        return -1;
    }
    if (opening_wrapper) {
        return 0; /* the stream's own outer '[': not a record, stays BETWEEN */
    }

    if (state->phase == JSP_PLUCK_SCALAR) {
        /* a bare scalar ends at the first whitespace or structural byte;
           process_run only ever sees the whitespace case, so a scalar
           butting straight up against this mask bit closes here instead */
        state->phase = JSP_PLUCK_BETWEEN;
        if (jsp_write_all(out_fd, (const unsigned char *)"\n", 1) != 0) {
            return -1;
        }
    }

    if (state->phase == JSP_PLUCK_STRING) {
        /* every mask bit strictly inside a string is cleared; only its own
           closing quote survives to reach here */
        if (jsp_write_all(out_fd, &c, 1) != 0) {
            return -1;
        }
        state->phase = JSP_PLUCK_BETWEEN;
        return jsp_write_all(out_fd, (const unsigned char *)"\n", 1);
    }

    if (state->phase == JSP_PLUCK_CONTAINER) {
        if (jsp_write_all(out_fd, &c, 1) != 0) {
            return -1;
        }
        if ((c == '}' || c == ']') && state->depth.depth == state->record_depth) {
            state->phase = JSP_PLUCK_BETWEEN;
            return jsp_write_all(out_fd, (const unsigned char *)"\n", 1);
        }
        return 0;
    }

    /* JSP_PLUCK_BETWEEN, a scalar just closed above included. */
    if (c == '"') {
        state->phase = JSP_PLUCK_STRING;
        return jsp_write_all(out_fd, &c, 1);
    }
    if (c == '{' || c == '[') {
        state->phase = JSP_PLUCK_CONTAINER;
        return jsp_write_all(out_fd, &c, 1);
    }
    /* ':', ',', or an already depth-checked '}'/']': the separator or
       closer between records, or the unwrapped array's own bracket. None
       of them belong to a record's own bytes. */
    return 0;
}

int jsp_pluck_step(jsp_pluck_state *state, jsp_slice block, uint64_t mask, int out_fd) {
    size_t pos = 0;
    size_t len = block.len;
    while (pos < len) {
        uint64_t remaining = mask & (~(uint64_t)0 << pos);
        if (remaining == 0) {
            return process_run(state, block.ptr, pos, len, out_fd);
        }
        size_t bit = (size_t)__builtin_ctzll(remaining);
        if (process_run(state, block.ptr, pos, bit, out_fd) != 0) {
            return -1;
        }
        if (step_structural(state, block.ptr[bit], out_fd) != 0) {
            return -1;
        }
        pos = bit + 1;
    }
    return 0;
}

int jsp_pluck_finish(jsp_pluck_state *state, int out_fd) {
    if (state->phase != JSP_PLUCK_SCALAR) {
        return 0;
    }
    state->phase = JSP_PLUCK_BETWEEN;
    return jsp_write_all(out_fd, (const unsigned char *)"\n", 1);
}

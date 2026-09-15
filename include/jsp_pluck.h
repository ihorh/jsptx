#ifndef JSP_PLUCK_H
#define JSP_PLUCK_H

#include "jsp_nesting.h"
#include "jsp_path.h"
#include "jsp_scan.h"
#include "jstr.h"

#include <stdint.h>

/* Where the walk sits relative to the value it is currently between or
   inside. A record is a top-level value, except that every top-level array
   unwraps and its elements are the records instead. The path picks a value
   out of each record, and "." picks the record itself. BETWEEN covers
   everything the path passes over. A value is found the moment BETWEEN sees
   its first byte at the path's end, and the value's own kind decides which
   phase follows. */
typedef enum {
    JSP_PLUCK_BETWEEN,
    JSP_PLUCK_STRING,      /* a string value, open quote already emitted */
    JSP_PLUCK_CONTAINER,   /* an object or array value, tracked via depth */
    JSP_PLUCK_SCALAR,      /* a bare number, true, false, or null value */
    JSP_PLUCK_KEY,         /* a key where the path's next segment could match */
    JSP_PLUCK_SKIP_STRING, /* any other string, or a key that stopped matching */
} jsp_pluck_phase;

/* Carried across every token of a stream, in order. Zero-initialize for the
   first token, which starts phase BETWEEN and nesting empty, then set
   separator and path. */
typedef struct {
    jsp_pluck_phase   phase;
    jstr              separator; /* written before every value but the first */
    jsp_path          path;
    _Bool             scalars_only; /* refuse a container value rather than emit it */
    _Bool             emitted_any;  /* at least one value was emitted */
    jsp_nesting_state nesting;
    unsigned          segments_matched;  /* keys that led here; drops as their objects close */
    jstr              segment_text_left; /* path bytes the open key has yet to match */
    uint8_t           last_structural;   /* the last of { } [ ] : , " seen */
} jsp_pluck_state;

/* Why jsp_pluck_push stopped. The three failures all leave the stream where
   it was: the token that was pushed is the one at fault, so its offset locates
   the error. MALFORMED is an unbalanced or mismatched bracket, or nesting past
   JSP_MAX_DEPTH. */
typedef enum {
    JSP_PLUCK_OK,
    JSP_PLUCK_WRITE_FAILED,      /* a write to out_fd failed; errno is set */
    JSP_PLUCK_MALFORMED,         /* the bracket just pushed breaks nesting */
    JSP_PLUCK_CONTAINER_REFUSED, /* the path picked a container under scalars_only */
} jsp_pluck_status;

/* Takes one token, writing each value the path picks, its own bytes verbatim,
   straight to out_fd, with separator before every value but the first. A
   segment matches a key byte for byte, escapes included. Call once per token
   from jsp_scan_next, in order, for every BYTES and STRUCTURAL the stream
   yields. With scalars_only, a container value stops the walk before any of
   its bytes, its separator included, reach out_fd. */
jsp_pluck_status jsp_pluck_push(jsp_pluck_state *state, jsp_token token, int out_fd);

#endif /* JSP_PLUCK_H */

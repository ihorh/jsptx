#ifndef JSP_SCAN_H
#define JSP_SCAN_H

#include "jsp_reader.h"
#include "jsp_slice_u8.h"
#include "jsp_string_mask.h"
#include "jsp_trace.h"

#include <stddef.h>
#include <stdint.h>

/* One classification covers this many octets, which is the classifier's
   vector width rather than anything the stream decides. */
#define JSP_SCAN_BLOCK 64

/* What a token is made of.

   These two cover every octet of the input exactly once, so a consumer that
   copies both writes the stream back verbatim. They stay the only two on
   purpose: a kind naming something the caller decides — a key, a record
   boundary — would make this a vocabulary for one caller rather than an
   iterator over set bits. */
typedef enum {
    JSP_TOKEN_RUN,        /* bytes holds no structural octet */
    JSP_TOKEN_STRUCTURAL, /* bytes.len is 1, and it is one of { } [ ] : , " */
} jsp_token_kind;

/* bytes views the scan's own buffer, so the next call invalidates it. offset
   locates bytes.ptr[0] in the stream. */
typedef struct {
    jsp_token_kind kind;
    jsp_slice_u8   bytes;
    uint64_t       offset;
} jsp_token;

/* Why a call returned. Exhaustion and a read failure are facts about the
   source rather than kinds of token, so they sit beside the token the way
   jsp_reader_status sits beside a block. */
typedef enum {
    JSP_SCAN_TOKEN,
    JSP_SCAN_END,   /* the stream is exhausted */
    JSP_SCAN_ERROR, /* a read, or a trace write, failed; errno is set */
} jsp_scan_status;

/* token is meaningful only when status is JSP_SCAN_TOKEN. */
typedef struct {
    jsp_scan_status status;
    jsp_token       token;
} jsp_scan_result;

/* What is left of the block being walked, with its classification and its
   place in the stream. One invariant ties the three together:

       bit i of mask describes rest.ptr[i], which sits at offset + i.

   **Read the mask right to left.** Bit 0 is the block's first octet, so the
   mask runs the opposite way from the octets it describes, and printing it as
   hex puts the *last* octet's bit leftmost. Picturing index zero at the right
   end of the buffer makes the two agree. Consuming n octets is therefore a
   right shift of n, which drops exactly the bits just walked past. */
typedef struct {
    jsp_slice_u8 rest;
    uint64_t     mask;
    uint64_t     offset;
} jsp_scan_window;

/* Turns a descriptor's octets into tokens: classify, mask off what a string
   quotes, trim the trailing block to its real length, and walk the set bits.

   The block boundary stops here. A run split by a refill arrives as two runs,
   which a consumer copying octets or matching them incrementally handles
   without noticing.

   Zero-initialize nothing by hand; jsp_scan_init does it. */
typedef struct {
    jsp_reader       reader;
    jsp_trace        trace;
    jsp_string_state string;
    jsp_scan_window  window;
} jsp_scan;

/* buf must hold a multiple of JSP_SCAN_BLOCK octets, at least one of them, and
   be initialized: the classifier reads a full block whatever the last one's
   real length, and the octets past that length reach no output because the
   trim clears every bit they produce. */
void jsp_scan_init(jsp_scan *s, int fd, jsp_buf_u8 buf, jsp_trace trace);

/* The next token, or the reason there is none. Never yields an empty run. */
jsp_scan_result jsp_scan_next(jsp_scan *s);

#endif /* JSP_SCAN_H */

#ifndef JSP_SLICE_U8_H
#define JSP_SLICE_U8_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Every span here is a span of octets, which the _u8 in each name states
   rather than leaves to the field types. jsptx reads bytes off a descriptor
   and classifies them eight bits at a time, so the assumption is real and
   worth failing the build over.

   uint8_t is optional in C99: an implementation provides it only when it has
   an unsigned type of exactly 8 bits with no padding. Every target this
   project builds for has one, and using it consistently beats spelling the
   same guarantee as `unsigned char` plus a comment. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(CHAR_BIT == 8, "jsptx assumes an 8-bit byte");
#else
typedef char jsp_octet_byte_assert_[CHAR_BIT == 8 ? 1 : -1];
#endif

/* A borrowed, read-only view of len octets at ptr. Owns nothing, so it dies
   whenever the memory behind it moves, and every stage takes it by value. */
typedef struct {
    const uint8_t *ptr;
    size_t         len;
} jsp_slice_u8;

static inline jsp_slice_u8 jsp_slice_u8_make(const uint8_t *ptr, size_t len) {
    return (jsp_slice_u8){ptr, len};
}

/* A borrowed, writable region: cap octets at ptr, of which the first len hold
   data. Owns nothing either. Carrying both sizes is the whole point, since a
   producer fills toward cap while a consumer reads len. */
typedef struct {
    uint8_t *ptr;
    size_t   len;
    size_t   cap;
} jsp_buf_u8;

static inline jsp_buf_u8 jsp_buf_u8_make(uint8_t *ptr, size_t cap) {
    return (jsp_buf_u8){ptr, 0, cap};
}

/* Where the next write goes, and how much room is left for it. */
static inline uint8_t *jsp_buf_u8_tail(jsp_buf_u8 b) { return b.ptr + b.len; }
static inline size_t   jsp_buf_u8_room(jsp_buf_u8 b) { return b.cap - b.len; }

/* Drops the first n octets, moving what follows to the front. n is at most
   len, which every caller holds by construction. */
static inline void jsp_buf_u8_compact(jsp_buf_u8 *b, size_t n) {
    assert(n <= b->len);
    b->len -= n;
    if (b->len > 0 && n > 0) {
        memmove(b->ptr, b->ptr + n, b->len);
    }
}

#endif /* JSP_SLICE_U8_H */

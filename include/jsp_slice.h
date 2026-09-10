#ifndef JSP_SLICE_H
#define JSP_SLICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A borrowed, read-only view of len bytes at ptr. Owns nothing, so it dies
   whenever the memory behind it moves, and every stage takes it by value. */
typedef struct {
    const uint8_t *ptr;
    size_t         len;
} jsp_slice;

static inline jsp_slice jsp_slice_make(const uint8_t *ptr, size_t len) {
    return (jsp_slice){ptr, len};
}

/* A borrowed, writable region: cap bytes at ptr, of which the first len hold
   data. Owns nothing either. Carrying both sizes is the whole point, since a
   producer fills toward cap while a consumer reads len. */
typedef struct {
    uint8_t *ptr;
    size_t   len;
    size_t   cap;
} jsp_buf;

static inline jsp_buf jsp_buf_make(uint8_t *ptr, size_t cap) {
    return (jsp_buf){ptr, 0, cap};
}

/* Where the next write goes, and how much room is left for it. */
static inline uint8_t *jsp_buf_free(jsp_buf b) { return b.ptr + b.len; }
static inline size_t   jsp_buf_room(jsp_buf b) { return b.cap - b.len; }

/* Drops the first n bytes, moving what follows to the front. */
static inline void jsp_buf_compact(jsp_buf *b, size_t n) {
    b->len -= n;
    if (b->len > 0 && n > 0) {
        memmove(b->ptr, b->ptr + n, b->len);
    }
}

#endif /* JSP_SLICE_H */

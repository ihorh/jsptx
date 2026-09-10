#ifndef JSP_SLICE_H
#define JSP_SLICE_H

#include <stddef.h>
#include <stdint.h>

/* A borrowed, read-only view of len bytes at ptr. Owns nothing, so it dies
   whenever the memory behind it moves, and every stage takes it by value. */
typedef struct {
    const uint8_t *ptr;
    size_t         len;
} jsp_slice;

static inline jsp_slice jsp_slice_make(const uint8_t *ptr, size_t len) {
    return (jsp_slice){ptr, len};
}

#endif /* JSP_SLICE_H */

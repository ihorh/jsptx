#ifndef JSP_BLOCK_H
#define JSP_BLOCK_H

#include <stdint.h>

/* A view over one block of input, borrowed and never owned. len is how many
   bytes are real input, from 1 to JSP_BLOCK_MAX; bytes may have readable
   octets past len, which the caller padded, and which no stage reacts to. */
typedef struct jsp_block {
    const uint8_t *bytes;
    unsigned       len;
} jsp_block;

static inline jsp_block jsp_block_make(const uint8_t *bytes, unsigned len) {
    return (jsp_block){bytes, len};
}

#endif /* JSP_BLOCK_H */

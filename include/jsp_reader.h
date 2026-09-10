#ifndef JSP_READER_H
#define JSP_READER_H

#include "jsp_slice.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    JSP_READER_BLOCK, /* block holds the next block, full or the trailing partial one */
    JSP_READER_END,   /* the stream is exhausted; no block follows */
    JSP_READER_ERROR, /* read(2) failed; errno is set */
} jsp_reader_status;

/* block is meaningful only when status is JSP_READER_BLOCK. */
typedef struct {
    jsp_reader_status status;
    jsp_slice         block;
} jsp_reader_result;

/* Cuts a descriptor's bytes into fixed-width blocks.

   block_size and filler are the caller's, since the reader knows no reason to
   prefer one width or one padding byte over another: block_size is whatever
   the caller's next stage consumes at a time, and filler is a byte that stage
   treats as inert. buf.cap must be a multiple of block_size, and at least one
   of them, which is what keeps a block inside the buffer.

   pos is how much of buf.len has already been handed out. */
typedef struct {
    int      fd;
    jsp_buf  buf;
    size_t   block_size;
    uint8_t  filler;
    size_t   pos;
    bool     done; /* the trailing partial block, if any, has already been returned */
} jsp_reader;

void jsp_reader_init(jsp_reader *r, int fd, jsp_buf buf, size_t block_size, uint8_t filler);

/* Reports which of the three ways the stream answered, with the next block
   when it answered with one. Never yields a zero-length block: a stream
   ending on a block boundary goes straight to JSP_READER_END rather than one
   more, empty, block.

   A trailing partial block carries its real length, and the bytes from there
   to block_size hold filler. A stage that reads the full width regardless
   therefore sees something inert, and one that honours the length never looks.

   The returned slice views the reader's own buffer, so the next call to this
   function invalidates it. */
jsp_reader_result jsp_reader_next(jsp_reader *r);

#endif /* JSP_READER_H */

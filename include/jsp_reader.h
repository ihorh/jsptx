#ifndef JSP_READER_H
#define JSP_READER_H

#include "jsp_slice_u8.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    JSP_READER_BLOCK, /* block holds the next block, full or the trailing partial one */
    JSP_READER_END,   /* the stream is exhausted; no block follows */
    JSP_READER_ERROR, /* read(2) failed; errno is set */
} jsp_reader_status;

/* block and offset are meaningful only when status is JSP_READER_BLOCK. */
typedef struct {
    jsp_reader_status status;
    jsp_slice_u8      block;
    uint64_t          offset; /* of block.ptr[0] in the stream */
} jsp_reader_result;

/* Cuts a descriptor's bytes into fixed-width blocks.

   block_size is the caller's, since the reader knows no reason to prefer one
   width: it is whatever the caller's next stage consumes at a time. buf.cap
   must be a multiple of block_size, and at least one of them, which is what
   keeps a block inside the buffer.

   buf_offset is how far into buf the reader has handed out, so
   buf_offset <= buf.len <= buf.cap, and compaction returns it to zero.
   stream_offset is how far into the stream it has handed out, counted from
   the first octet ever read and never reset, which is where the next block's
   offset comes from. It is the wider type because a stream can outrun the
   address space where a buffer cannot. */
typedef struct {
    int        fd;
    jsp_buf_u8 buf;
    size_t     block_size;
    size_t     buf_offset;    /* into buf; zeroed by compaction */
    uint64_t   stream_offset; /* into the stream; never reset */
    _Bool      done; /* the trailing partial block, if any, has already been returned */
} jsp_reader;

void jsp_reader_init(jsp_reader *r, int fd, jsp_buf_u8 buf, size_t block_size);

/* Reports which of the three ways the stream answered, with the next block
   when it answered with one. Never yields a zero-length block: a stream
   ending on a block boundary goes straight to JSP_READER_END rather than one
   more, empty, block.

   block.len is what the stream supplied, and the block_size octets at
   block.ptr are all readable, so a stage taking the full width regardless
   reaches nothing unallocated. What the octets past block.len hold is
   whatever the caller's buffer already held: the reader writes none of them,
   since it knows neither what reads them nor what would be inert to it.

   The returned slice views the reader's own buffer, so the next call to this
   function invalidates it. */
jsp_reader_result jsp_reader_next(jsp_reader *r);

#endif /* JSP_READER_H */

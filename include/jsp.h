#ifndef JSP_H
#define JSP_H

#include <stddef.h>

/* What jsp_run does with each block's result. */
typedef enum {
    JSP_OUTPUT_OFFSETS, /* one line per structural character: "<offset>\t<char>\n" */
    JSP_OUTPUT_MASKS,   /* one line per block: "<offset>\t<mask>\n" */
    JSP_OUTPUT_SINK,    /* classifies and discards; out_fd is never touched */
} jsp_output_mode;

/* Reads in_fd until end of file, classifying the structural JSON characters
   { } [ ] : , " in 64-byte blocks, and writes the result to out_fd.

   With JSP_OUTPUT_OFFSETS, writes one line per structural character found:
   "<offset>\t<char>\n", offset absolute in the input stream. With
   JSP_OUTPUT_MASKS, writes one line per block instead: "<offset>\t<mask>\n",
   mask the block's 64-bit classification as 16 lowercase hex digits and
   offset the block's first byte. With JSP_OUTPUT_SINK, writes nothing at
   all: every block is still classified, but the result is discarded rather
   than formatted and written, for measuring the pipeline's own cost apart
   from its I/O.

   Rounds buf_size up to the nearest multiple of 64, with a minimum of 64,
   and reads in chunks of that size. Returns 0 on success, -1 on a read or
   write error with errno set by the failing call. */
int jsp_run(int in_fd, int out_fd, size_t buf_size, jsp_output_mode mode);

#endif /* JSP_H */

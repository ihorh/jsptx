#ifndef JSP_H
#define JSP_H

#include <stdbool.h>
#include <stddef.h>

/* Reads in_fd until end of file, classifying the structural JSON characters
   { } [ ] : , " in 64-byte blocks, and writes the result to out_fd.

   With masks false, writes one line per structural character found:
   "<offset>\t<char>\n", offset absolute in the input stream. With masks
   true, writes one line per block instead: "<offset>\t<mask>\n", mask the
   block's 64-bit classification as 16 lowercase hex digits and offset the
   block's first byte.

   Rounds buf_size up to the nearest multiple of 64, with a minimum of 64,
   and reads in chunks of that size. Returns 0 on success, -1 on a read or
   write error with errno set by the failing call. */
int jsp_run(int in_fd, int out_fd, size_t buf_size, bool masks);

#endif /* JSP_H */

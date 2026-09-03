#ifndef JSP_H
#define JSP_H

#include <stddef.h>

/* Reads in_fd until end of file and writes every byte read to out_fd. Rounds
   buf_size up to the nearest multiple of 64, with a minimum of 64, and reads
   in chunks of that size. Returns 0 on success, -1 on a read or write error
   with errno set by the failing call. */
int jsp_run(int in_fd, int out_fd, size_t buf_size);

#endif /* JSP_H */

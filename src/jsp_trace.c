#include "jsp_trace.h"

#include "jsp_io.h"

#include <inttypes.h>
#include <stdio.h>

static int trace_masks(jsp_trace t, uint64_t offset, uint64_t mask) {
    char line[40];
    int  len = snprintf(line, sizeof(line), "%" PRIu64 "\t%016" PRIx64 "\n", offset, mask);
    return jsp_write_all(t.fd, (unsigned char *)line, (size_t)len);
}

static int trace_offsets(jsp_trace t, uint64_t offset, jsp_slice_u8 block, uint64_t mask) {
    while (mask != 0) {
        unsigned bit = (unsigned)__builtin_ctzll(mask);
        mask &= mask - 1;

        char line[32];
        int  len =
            snprintf(line, sizeof(line), "%" PRIu64 "\t%c\n", offset + bit, block.ptr[bit]);
        if (jsp_write_all(t.fd, (unsigned char *)line, (size_t)len) != 0) {
            return -1;
        }
    }
    return 0;
}

int jsp_trace_block(jsp_trace t, uint64_t offset, jsp_slice_u8 block, uint64_t mask) {
    switch (t.mode) {
    case JSP_TRACE_OFFSETS:
        return trace_offsets(t, offset, block, mask);
    case JSP_TRACE_MASKS:
        return trace_masks(t, offset, mask);
    case JSP_TRACE_NONE:
    default:
        return 0;
    }
}

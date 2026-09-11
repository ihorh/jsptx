#include "jsp_run.h"

#include "jsp_io.h"
#include "jsp_pluck.h"
#include "jsp_scan.h"
#include "jsp_slice_u8.h"
#include "jsp_trace.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/* The smallest multiple of `multiple` that is at least n, and never zero. */
static size_t round_up_to(size_t n, size_t multiple) {
    assert(multiple > 0);
    if (n < multiple) {
        return multiple;
    }
    return (n + multiple - 1) / multiple * multiple;
}

static inline jsp_result jsp_result_make_(jsp_status status) {
    return (jsp_result){.status = status, .sys_errno = status == JSP_OK ? 0 : errno};
}

jsp_result jsp_run(jsp_fds fds, jsp_settings settings) {
    size_t cap = round_up_to(settings.buf_size, JSP_SCAN_BLOCK);
    /* calloc, since the classifier reads a whole JSP_SCAN_BLOCK even where
       the trailing block holds fewer octets. */
    uint8_t *buf = calloc(cap, 1);
    if (buf == NULL) {
        return jsp_result_make_(JSP_ERR_ALLOC);
    }

    jsp_scan scan;
    jsp_scan_init(&scan, fds.in_fd, jsp_buf_u8_make(buf, cap),
                  jsp_trace_make(fds.trace_fd, settings.trace));

    jsp_status      result = JSP_OK;
    jsp_pluck_state pluck_state = {0};
    bool            plucking = settings.output == JSP_OUTPUT_PLUCK;

    for (;;) {
        jsp_scan_result next = jsp_scan_next(&scan);
        /* clang-format off */
        if (next.status == JSP_SCAN_END)   { break; }
        if (next.status == JSP_SCAN_ERROR) { result = JSP_ERR_IO; break; }
        /* clang-format on */
        if (plucking && jsp_pluck_push(&pluck_state, next.token, fds.out_fd) != 0) {
            result = JSP_ERR_BLOCK_PROCESS_TMP;
            break;
        }
    }

    if (result == JSP_OK && plucking) {
        result = jsp_pluck_finish(&pluck_state, fds.out_fd) == 0 ? JSP_OK
                                                                 : JSP_ERR_BLOCK_PROCESS_TMP;
    }

    free(buf);
    return jsp_result_make_(result);
}

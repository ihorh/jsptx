#include "jsp_run.h"
#include "jsp_settings.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    jsp_fds fds = {
        .in_fd = STDIN_FILENO,
        .out_fd = STDOUT_FILENO,
        .err_fd = STDERR_FILENO,
        .trace_fd = STDERR_FILENO,
    };
    jsp_settings settings = jsp_settings_parse(argc, argv);

    jsp_result result = jsp_run(fds, settings);

    switch (result.status) {
    case JSP_OK:
        break;
    case JSP_ERR_ALLOC:
    case JSP_ERR_IO:
        fprintf(stderr, "jsptx: %s\n", strerror(result.sys_errno));
        break;
    case JSP_ERR_MALFORMED:
        fprintf(stderr, "jsptx: malformed input at byte %" PRIu64 "\n", result.offset);
        break;
    case JSP_ERR_LINES_CONTAINER:
        fprintf(stderr,
                "jsptx: byte %" PRIu64
                " starts an object or array, which --lines cannot print\n",
                result.offset);
        break;
    }
    return (int)result.status;
}

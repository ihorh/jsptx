#define _POSIX_C_SOURCE 200809L

/* Throughput of jsp_run in JSP_OUTPUT_SINK mode: classification and string
 * masking, with the cost of formatting and writing a result removed.
 * `--masks`/`--offsets` emit more bytes than they read, so a throughput
 * number measured through one of those modes measures snprintf and write,
 * not the pipeline. Not a test — asserts nothing and is not run by
 * `zig build test`. Run it (`zig build bench`) when the classify or
 * string-mask stages change, and read the table.
 */
#include "jsp.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define INPUT_SIZE ((size_t)64 * 1024 * 1024) /* 64 MiB */

/* A small JSON object, repeated until the buffer is full: every repeat
   exercises the classifier on every structural kind, and string_mask on an
   open quote, a close quote, and one escape. Deterministic and reproducible
   across runs, which matters more here than resembling real-world JSON. */
static uint8_t *make_input(size_t size) {
    static const char unit[] = "{\"key\":\"value\\\"escaped\",\"n\":123,\"arr\":[1,2,3]}\n";
    size_t            unit_len = sizeof(unit) - 1;

    uint8_t *buf = malloc(size);
    if (buf == NULL) {
        fprintf(stderr, "jsp_run_bench: out of memory allocating %zu bytes\n", size);
        exit(1);
    }
    for (size_t pos = 0; pos < size;) {
        size_t n = size - pos < unit_len ? size - pos : unit_len;
        memcpy(buf + pos, unit, n);
        pos += n;
    }
    return buf;
}

/* An unlinked temp file holding input, so jsp_run reads through a real fd
   backed by the page cache rather than a pipe: sink mode wants the
   pipeline's own cost, not pipe scheduling noise. */
static int make_input_file(const uint8_t *input, size_t len) {
    char tmpl[] = "/tmp/jsptx_bench_XXXXXX";
    int  fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        exit(1);
    }
    unlink(tmpl);

    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, input + written, len - written);
        if (n < 0) {
            perror("write");
            exit(1);
        }
        written += (size_t)n;
    }
    return fd;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void run_at(int fd, size_t input_len, size_t buf_size) {
    if (lseek(fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        exit(1);
    }

    double start = now();
    int    result = jsp_run(fd, -1, buf_size, JSP_OUTPUT_SINK);
    double elapsed = now() - start;
    if (result != 0) {
        perror("jsp_run");
        exit(1);
    }

    double mb_per_s = (double)input_len / (1024.0 * 1024.0) / elapsed;
    double ns_per_block = elapsed * 1e9 / ((double)input_len / 64.0);
    printf("  %10zu   %9.1f MB/s   %6.2f ns/block\n", buf_size, mb_per_s, ns_per_block);
}

int main(void) {
    static const size_t buf_sizes[] = {4096, 65536, 1024 * 1024};

    uint8_t *input = make_input(INPUT_SIZE);
    int      fd = make_input_file(input, INPUT_SIZE);
    free(input);

    printf("jsp_run, JSP_OUTPUT_SINK, %.0f MiB input\n\n",
           (double)INPUT_SIZE / (1024.0 * 1024.0));
    printf("    buf_size    throughput      per block\n");
    printf("  ----------   -----------      ---------\n");
    for (size_t i = 0; i < sizeof(buf_sizes) / sizeof(buf_sizes[0]); i++) {
        run_at(fd, INPUT_SIZE, buf_sizes[i]);
    }

    close(fd);
    return 0;
}

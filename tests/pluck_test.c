#define _POSIX_C_SOURCE 200809L

/* Feeds the fixtures under tests/data/pluck/ through jsp_run in
   JSP_OUTPUT_PLUCK mode and checks the record stream against each
   fixture's .expected file.

   Runs each fixture at buf_size 64 (the tightest possible refill) and at
   4096, carrying forward the same cross-buffer-size criterion
   tests/strings_test.c already checks for the offset stream: the same
   input must produce the same records no matter how the reads land. */

#include "jsp_run.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const FIXTURES[] = {
    "ndjson_objects", "array_unwrap", "bare_scalars",   "array_of_scalars",
    "strings",        "nested",       "block_boundary", "multi_array_unwrap",
};

/* Bytes of context printed on each side of the first difference. */
enum { DIFF_CONTEXT = 24 };

static int cases;
static int failures;

/* Reads an entire file into a malloc'd buffer; *out_len is its size. Aborts
   on any error, since a missing or unreadable fixture is a broken test, not
   a case to assert against. */
static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        abort();
    }
    int sought = fseek(f, 0, SEEK_END);
    assert(sought == 0);
    long size = ftell(f);
    assert(size >= 0);
    sought = fseek(f, 0, SEEK_SET);
    assert(sought == 0);

    uint8_t *buf = malloc((size_t)size ? (size_t)size : 1);
    assert(buf != NULL);
    size_t got = fread(buf, 1, (size_t)size, f);
    assert(got == (size_t)size);
    fclose(f);

    *out_len = (size_t)size;
    return buf;
}

static void write_all_blocking(int fd, const uint8_t *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("write");
            abort();
        }
        sent += (size_t)n;
    }
}

static void read_all_blocking(int fd, uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = read(fd, buf + got, len - got);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            abort();
        }
        assert(n > 0);
        got += (size_t)n;
    }
}

static int open_sink(void) {
    char tmpl[] = "/tmp/jsptx_pluck_test_XXXXXX";
    int  fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        abort();
    }
    unlink(tmpl);
    return fd;
}

static const char *status_name(jsp_status status) {
    switch (status) {
    case JSP_OK:
        return "JSP_OK";
    case JSP_ERR_ALLOC:
        return "JSP_ERR_ALLOC";
    case JSP_ERR_IO:
        return "JSP_ERR_IO";
    case JSP_ERR_BLOCK_PROCESS_TMP:
        return "JSP_ERR_BLOCK_PROCESS_TMP";
    }
    return "(unknown jsp_status)";
}

/* Writes buf[from, to), clamped to len, to stderr as a C string literal, so
   a newline, or a missing one, shows up rather than shaping the output. An
   ellipsis marks bytes cut off on either side. */
static void print_window(const uint8_t *buf, size_t len, size_t from, size_t to) {
    if (to > len) {
        to = len;
    }
    fputs(from > 0 ? "...\"" : "\"", stderr);
    for (size_t i = from; i < to; i++) {
        uint8_t c = buf[i];
        if (c == '\n') {
            fputs("\\n", stderr);
        } else if (c == '\t') {
            fputs("\\t", stderr);
        } else if (c == '"' || c == '\\') {
            fprintf(stderr, "\\%c", c);
        } else if (c < 0x20 || c >= 0x7f) {
            fprintf(stderr, "\\x%02x", c);
        } else {
            fputc(c, stderr);
        }
    }
    fputs(to < len ? "\"...\n" : "\"\n", stderr);
}

/* Checks one case's jsp_run status and output against want. On a mismatch it
   reports the case by name, with the first differing byte and the bytes
   around it on both sides, and counts a failure rather than aborting, so a
   run shows every case a change breaks. */
static void check_case(const char *name, jsp_result result, const uint8_t *got, size_t got_len,
                       const uint8_t *want, size_t want_len) {
    cases++;
    bool ok = true;

    if (result.status != JSP_OK) {
        fprintf(stderr, "FAIL %s: jsp_run returned %s (sys_errno %d)\n", name,
                status_name(result.status), result.sys_errno);
        ok = false;
    }

    size_t common = got_len < want_len ? got_len : want_len;
    size_t diff = 0;
    while (diff < common && got[diff] == want[diff]) {
        diff++;
    }
    if (diff < got_len || diff < want_len) {
        size_t line = 1;
        for (size_t i = 0; i < diff; i++) {
            if (want[i] == '\n') {
                line++;
            }
        }
        fprintf(stderr,
                "FAIL %s: got %zu bytes, want %zu; first difference at byte %zu, line %zu\n",
                name, got_len, want_len, diff, line);
        size_t from = diff > DIFF_CONTEXT ? diff - DIFF_CONTEXT : 0;
        fputs("  got:  ", stderr);
        print_window(got, got_len, from, diff + DIFF_CONTEXT);
        fputs("  want: ", stderr);
        print_window(want, want_len, from, diff + DIFF_CONTEXT);
        ok = false;
    }

    if (!ok) {
        failures++;
    }
}

/* What jsp_run returned for one input, and what it wrote to out_fd. */
typedef struct {
    jsp_result result;
    uint8_t   *out;
    size_t     out_len;
} pluck_run;

/* Feeds input through a pipe into jsp_run in JSP_OUTPUT_PLUCK mode and
   reads back the record stream. The caller frees .out. */
static pluck_run run_pluck(const uint8_t *input, size_t input_len, size_t buf_size) {
    int in_pipe[2];
    int piped = pipe(in_pipe);
    assert(piped == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(in_pipe[0]);
        write_all_blocking(in_pipe[1], input, input_len);
        close(in_pipe[1]);
        _exit(0);
    }
    close(in_pipe[1]);

    int          out_fd = open_sink();
    jsp_fds      fds = {.in_fd = in_pipe[0], .out_fd = out_fd, .trace_fd = -1, .err_fd = -1};
    jsp_settings settings = {
        .buf_size = buf_size, .output = JSP_OUTPUT_PLUCK, .trace = JSP_TRACE_NONE};
    pluck_run run = {.result = jsp_run(fds, settings)};
    close(in_pipe[0]);

    int   status;
    pid_t reaped = waitpid(pid, &status, 0);
    assert(reaped >= 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    off_t size = lseek(out_fd, 0, SEEK_END);
    assert(size >= 0);
    run.out_len = (size_t)size;
    run.out = malloc(run.out_len ? run.out_len : 1);
    assert(run.out != NULL);
    off_t rewound = lseek(out_fd, 0, SEEK_SET);
    assert(rewound == 0);
    read_all_blocking(out_fd, run.out, run.out_len);
    close(out_fd);
    return run;
}

/* Runs one fixture at one buf_size and checks jsp_run's record stream
   against the fixture's .expected file. */
static void run_fixture(const char *name, size_t buf_size) {
    char json_path[256];
    char expected_path[256];
    char case_name[256];
    snprintf(json_path, sizeof(json_path), "tests/data/pluck/%s.json", name);
    snprintf(expected_path, sizeof(expected_path), "tests/data/pluck/%s.expected", name);
    snprintf(case_name, sizeof(case_name), "pluck/%s buf_size=%zu", name, buf_size);

    size_t   input_len;
    uint8_t *input = read_file(json_path, &input_len);
    size_t   want_len;
    uint8_t *want = read_file(expected_path, &want_len);

    pluck_run run = run_pluck(input, input_len, buf_size);
    check_case(case_name, run.result, run.out, run.out_len, want, want_len);

    free(input);
    free(want);
    free(run.out);
}

/* A bare scalar with nothing after it, ending exactly on a 64-byte block
   boundary, so the reader's last block is the full one and the loop breaks
   on JSP_SCAN_END with no further block to trigger the scalar's close. */
static void test_scalar_on_block_boundary(void) {
    uint8_t input[64];
    memset(input, ' ', sizeof(input) - 1);
    input[sizeof(input) - 1] = '7';

    static const uint8_t want[] = "7\n";
    pluck_run            run = run_pluck(input, sizeof(input), 64);
    check_case("scalar_on_block_boundary buf_size=64", run.result, run.out, run.out_len, want,
               sizeof(want) - 1);
    free(run.out);
}

/* Depth errors (an unbalanced or mismatched closing bracket) fail the run
   rather than emitting a partial record. */
static void test_malformed_input_fails(void) {
    static const uint8_t input[] = "}";
    pluck_run            run = run_pluck(input, sizeof(input) - 1, 64);

    /* JSP_ERR_BLOCK_PROCESS_TMP is the status today, but its name says it is
       a placeholder; what this test pins is that the run fails at all. */
    cases++;
    if (run.result.status == JSP_OK) {
        fprintf(stderr, "FAIL malformed_input: jsp_run returned JSP_OK, want a failure\n");
        fputs("  input: ", stderr);
        print_window(input, sizeof(input) - 1, 0, sizeof(input) - 1);
        fputs("  got:   ", stderr);
        print_window(run.out, run.out_len, 0, run.out_len);
        failures++;
    }
    free(run.out);
}

int main(void) {
    static const size_t buf_sizes[] = {64, 4096};

    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); i++) {
        for (size_t j = 0; j < sizeof(buf_sizes) / sizeof(buf_sizes[0]); j++) {
            run_fixture(FIXTURES[i], buf_sizes[j]);
        }
    }

    test_scalar_on_block_boundary();
    test_malformed_input_fails();

    if (failures > 0) {
        fprintf(stderr, "pluck_test: %d of %d cases failed\n", failures, cases);
        return 1;
    }
    printf("ok\n");
    return 0;
}

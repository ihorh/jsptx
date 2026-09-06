#define _POSIX_C_SOURCE 200809L

/* Feeds the fixtures under tests/data/pluck/ through jsp_run in
   JSP_OUTPUT_PLUCK mode and checks the record stream against each
   fixture's .expected file.

   Runs each fixture at buf_size 64 (the tightest possible refill) and at
   4096, carrying forward the same cross-buffer-size criterion
   tests/strings_test.c already checks for the offset stream: the same
   input must produce the same records no matter how the reads land. */

#include "jsp.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const FIXTURES[] = {
    "ndjson_objects", "array_unwrap", "bare_scalars",   "array_of_scalars",
    "strings",        "nested",       "block_boundary",
};

/* Reads an entire file into a malloc'd buffer; *out_len is its size. Aborts
   on any error, since a missing or unreadable fixture is a broken test, not
   a case to assert against. */
static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        perror(path);
        abort();
    }
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size >= 0);
    assert(fseek(f, 0, SEEK_SET) == 0);

    uint8_t *buf = malloc((size_t)size ? (size_t)size : 1);
    assert(buf != NULL);
    assert(fread(buf, 1, (size_t)size, f) == (size_t)size);
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

/* Runs one fixture at one buf_size and asserts jsp_run's record stream
   matches the fixture's .expected file exactly. */
static void run_fixture(const char *name, size_t buf_size) {
    char json_path[256];
    char expected_path[256];
    snprintf(json_path, sizeof(json_path), "tests/data/pluck/%s.json", name);
    snprintf(expected_path, sizeof(expected_path), "tests/data/pluck/%s.expected", name);

    size_t   input_len;
    uint8_t *input = read_file(json_path, &input_len);
    size_t   want_len;
    uint8_t *want = read_file(expected_path, &want_len);

    int in_pipe[2];
    assert(pipe(in_pipe) == 0);
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
    assert(jsp_run(fds, settings) == 0);
    close(in_pipe[0]);

    int status;
    assert(waitpid(pid, &status, 0) >= 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    off_t got_size = lseek(out_fd, 0, SEEK_END);
    assert(got_size >= 0);
    uint8_t *got = malloc((size_t)got_size ? (size_t)got_size : 1);
    assert(got != NULL);
    assert(lseek(out_fd, 0, SEEK_SET) == 0);
    read_all_blocking(out_fd, got, (size_t)got_size);
    close(out_fd);

    if ((size_t)got_size != want_len || memcmp(got, want, want_len) != 0) {
        fprintf(stderr, "pluck/%s at buf_size=%zu: got %jd bytes, want %zu\n", name, buf_size,
                (intmax_t)got_size, want_len);
        fprintf(stderr, "--- got ---\n%.*s--- want ---\n%.*s", (int)got_size, got,
                (int)want_len, want);
        abort();
    }

    free(input);
    free(want);
    free(got);
}

/* jsp_pluck_finish's own reason to exist: a bare scalar with nothing after
   it, ending exactly on a 64-byte block boundary, so the reader's last
   block is the full one and the loop breaks on JSP_READER_END with no
   further block to trigger the scalar's close. */
static void test_scalar_on_block_boundary(void) {
    uint8_t input[64];
    memset(input, ' ', sizeof(input) - 1);
    input[sizeof(input) - 1] = '7';

    int in_pipe[2];
    assert(pipe(in_pipe) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(in_pipe[0]);
        write_all_blocking(in_pipe[1], input, sizeof(input));
        close(in_pipe[1]);
        _exit(0);
    }
    close(in_pipe[1]);

    int          out_fd = open_sink();
    jsp_fds      fds = {.in_fd = in_pipe[0], .out_fd = out_fd, .trace_fd = -1, .err_fd = -1};
    jsp_settings settings = {
        .buf_size = 64, .output = JSP_OUTPUT_PLUCK, .trace = JSP_TRACE_NONE};
    assert(jsp_run(fds, settings) == 0);
    close(in_pipe[0]);

    int status;
    assert(waitpid(pid, &status, 0) >= 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    static const uint8_t want[] = "7\n";
    off_t                got_size = lseek(out_fd, 0, SEEK_END);
    assert(got_size == (off_t)(sizeof(want) - 1));
    uint8_t got[8];
    assert(lseek(out_fd, 0, SEEK_SET) == 0);
    read_all_blocking(out_fd, got, (size_t)got_size);
    close(out_fd);
    assert(memcmp(got, want, (size_t)got_size) == 0);
}

/* Depth errors (an unbalanced or mismatched closing bracket) fail the run
   rather than emitting a partial record. */
static void test_malformed_input_fails(void) {
    static const uint8_t input[] = "}";

    int in_pipe[2];
    assert(pipe(in_pipe) == 0);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        close(in_pipe[0]);
        write_all_blocking(in_pipe[1], input, sizeof(input) - 1);
        close(in_pipe[1]);
        _exit(0);
    }
    close(in_pipe[1]);

    int          out_fd = open_sink();
    jsp_fds      fds = {.in_fd = in_pipe[0], .out_fd = out_fd, .trace_fd = -1, .err_fd = -1};
    jsp_settings settings = {
        .buf_size = 64, .output = JSP_OUTPUT_PLUCK, .trace = JSP_TRACE_NONE};
    assert(jsp_run(fds, settings) == -1);
    close(in_pipe[0]);
    close(out_fd);

    int status;
    assert(waitpid(pid, &status, 0) >= 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
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

    printf("ok\n");
    return 0;
}

/* Feeds the fixtures under tests/data/strings/ through jsp_run and checks
   the offset stream against each fixture's .expected file.

   Runs each fixture at buf_size 64 (the tightest possible refill) and at
   4096, so a string crossing a block boundary is exercised whether or not
   it also crosses a read boundary. M1's own cross-buffer-size criterion
   carries over here: the same input must produce the same offsets no
   matter how the reads land. */

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
    "basic",          "escaped_quote",          "escaped_backslash", "odd_backslash_run",
    "block_boundary", "backslash_run_boundary",
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
    char tmpl[] = "/tmp/jsptx_strings_test_XXXXXX";
    int  fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        abort();
    }
    unlink(tmpl);
    return fd;
}

/* Runs one fixture at one buf_size and asserts jsp_run's offset stream
   matches the fixture's .expected file exactly. */
static void run_fixture(const char *name, size_t buf_size) {
    char json_path[256];
    char expected_path[256];
    snprintf(json_path, sizeof(json_path), "tests/data/strings/%s.json", name);
    snprintf(expected_path, sizeof(expected_path), "tests/data/strings/%s.expected", name);

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

    int out_fd = open_sink();
    assert(jsp_run(in_pipe[0], out_fd, buf_size, false) == 0);
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
        fprintf(
            stderr, "strings/%s at buf_size=%zu: got %jd bytes, want %zu\n", name, buf_size,
            (intmax_t)got_size, want_len
        );
        fprintf(
            stderr, "--- got ---\n%.*s--- want ---\n%.*s", (int)got_size, got, (int)want_len,
            want
        );
        abort();
    }

    free(input);
    free(want);
    free(got);
}

int main(void) {
    static const size_t buf_sizes[] = {64, 4096};

    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); i++) {
        for (size_t j = 0; j < sizeof(buf_sizes) / sizeof(buf_sizes[0]); j++) {
            run_fixture(FIXTURES[i], buf_sizes[j]);
        }
    }

    printf("ok\n");
    return 0;
}

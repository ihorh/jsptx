#define _POSIX_C_SOURCE 200809L

#include "jsp.h"

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

static bool is_structural(uint8_t c) {
    static const char structural[] = "{}[]:,\"";
    for (size_t i = 0; i < sizeof(structural) - 1; i++) {
        if (c == (uint8_t)structural[i]) {
            return true;
        }
    }
    return false;
}

static void write_all_chunked(int fd, const uint8_t *buf, size_t len, size_t chunk) {
    size_t sent = 0;
    while (sent < len) {
        size_t want = chunk == 0 ? len - sent : chunk;
        if (want > len - sent) {
            want = len - sent;
        }
        ssize_t n = write(fd, buf + sent, want);
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

static void read_all(int fd, uint8_t *buf, size_t len) {
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

typedef struct {
    int   read_fd;
    pid_t pid;
} jsp_test_writer;

/* Forks a child that writes input into a pipe in chunks of chunk bytes (0
   means one write), then exits. Returns the pipe's read end and the child's
   pid; the child runs concurrently with the caller so a pipe larger than its
   kernel buffer cannot deadlock the test. Pair with reap_writer once read_fd
   is drained. */
static jsp_test_writer spawn_writer(const uint8_t *input, size_t input_len, size_t chunk) {
    int in_pipe[2];
    if (pipe(in_pipe) != 0) {
        perror("pipe");
        abort();
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        abort();
    }
    if (pid == 0) {
        close(in_pipe[0]);
        write_all_chunked(in_pipe[1], input, input_len, chunk);
        close(in_pipe[1]);
        _exit(0);
    }
    close(in_pipe[1]);
    return (jsp_test_writer){in_pipe[0], pid};
}

/* Waits for a spawn_writer child and asserts it exited cleanly. */
static void reap_writer(pid_t pid) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        abort();
    }
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* An unlinked temp file open for reading and writing: a seekable sink whose
   storage disappears once the caller closes the returned fd. */
static int open_sink(void) {
    char tmpl[] = "/tmp/jsptx_run_test_XXXXXX";
    int  fd = mkstemp(tmpl);
    if (fd < 0) {
        perror("mkstemp");
        abort();
    }
    unlink(tmpl);
    return fd;
}

/* Asserts fd holds exactly expected_len bytes, matching expected byte for
   byte, read back from the start. */
static void assert_sink_equals(int fd, const uint8_t *expected, size_t expected_len) {
    off_t size = lseek(fd, 0, SEEK_END);
    assert(size >= 0 && (size_t)size == expected_len);

    if (expected_len == 0) {
        return;
    }

    uint8_t *got = malloc(expected_len);
    assert(got != NULL);
    if (lseek(fd, 0, SEEK_SET) < 0) {
        perror("lseek");
        abort();
    }
    read_all(fd, got, expected_len);
    assert(memcmp(got, expected, expected_len) == 0);
    free(got);
}

/* The index stream jsp_run must produce for input, built the naive way: a
   left-to-right scan with no notion of a block. */
static uint8_t *build_offsets_golden(const uint8_t *input, size_t len, size_t *out_len) {
    size_t   cap = len * 24 + 1; /* worst case: every byte structural */
    uint8_t *out = malloc(cap);
    assert(out != NULL);

    size_t pos = 0;
    for (size_t i = 0; i < len; i++) {
        if (is_structural(input[i])) {
            int n = snprintf((char *)out + pos, cap - pos, "%zu\t%c\n", i, input[i]);
            assert(n > 0);
            pos += (size_t)n;
        }
    }
    *out_len = pos;
    return out;
}

/* The mask stream jsp_run must produce for input: one line per 64-byte
   block, full or partial, in the same left-to-right order. */
static uint8_t *build_masks_golden(const uint8_t *input, size_t len, size_t *out_len) {
    size_t   nblocks = len == 0 ? 0 : (len + 63) / 64;
    size_t   cap = nblocks * 40 + 1;
    uint8_t *out = malloc(cap ? cap : 1);
    assert(out != NULL);

    size_t pos = 0;
    for (size_t b = 0; b < nblocks; b++) {
        size_t   base = b * 64;
        size_t   block_len = len - base < 64 ? len - base : 64;
        uint64_t mask = 0;
        for (size_t i = 0; i < block_len; i++) {
            if (is_structural(input[base + i])) {
                mask |= (uint64_t)1 << i;
            }
        }
        int n = snprintf((char *)out + pos, cap - pos, "%zu\t%016" PRIx64 "\n", base, mask);
        assert(n > 0);
        pos += (size_t)n;
    }
    *out_len = pos;
    return out;
}

/* Feeds input through a pipe into jsp_run, in chunks of chunk bytes (0 means
   one write), and asserts the index or mask stream that lands in a temp
   file matches the golden built independently for that input. */
static void
run_case(const uint8_t *input, size_t input_len, size_t chunk, size_t buf_size, bool masks) {
    jsp_test_writer w = spawn_writer(input, input_len, chunk);
    int             out_fd = open_sink();

    assert(jsp_run(w.read_fd, out_fd, buf_size, masks) == 0);
    close(w.read_fd);
    reap_writer(w.pid);

    size_t   golden_len;
    uint8_t *golden = masks ? build_masks_golden(input, input_len, &golden_len)
                            : build_offsets_golden(input, input_len, &golden_len);
    assert_sink_equals(out_fd, golden, golden_len);
    free(golden);
    close(out_fd);
}

/* A mix of structural and non-structural bytes, long enough to cross many
   block boundaries at every buffer size under test. */
static uint8_t *make_mixed(size_t len) {
    static const char structural[] = "{}[]:,\"";
    uint8_t          *buf = malloc(len ? len : 1);
    assert(buf != NULL);
    for (size_t i = 0; i < len; i++) {
        /* One byte in five is structural, cycling through all seven kinds;
           the rest are ordinary ASCII that is never structural. */
        buf[i] = (i % 5 == 0) ? (uint8_t)structural[i % (sizeof(structural) - 1)]
                              : (uint8_t)('a' + (i % 26));
    }
    return buf;
}

/* Runs one input, in both output modes, across every buffer size and chunk
   size M1's acceptance criteria name: 64, 65, 4096, and 1 MiB, delivered
   both in one write and trickled a byte at a time. */
static void run_matrix(const uint8_t *input, size_t input_len) {
    static const size_t buf_sizes[] = {64, 65, 4096, 1024 * 1024};
    static const size_t chunks[] = {0, 1};

    for (size_t i = 0; i < sizeof(buf_sizes) / sizeof(buf_sizes[0]); i++) {
        for (size_t j = 0; j < sizeof(chunks) / sizeof(chunks[0]); j++) {
            run_case(input, input_len, chunks[j], buf_sizes[i], false);
            run_case(input, input_len, chunks[j], buf_sizes[i], true);
        }
    }
}

int main(void) {
    /* Empty input: no blocks at all. */
    run_matrix(NULL, 0);

    /* One byte, structural and not. */
    uint8_t one_structural = '{';
    run_matrix(&one_structural, 1);
    uint8_t one_plain = 'x';
    run_matrix(&one_plain, 1);

    /* Exactly one block, and one block plus a one-byte tail. */
    uint8_t *exact = make_mixed(64);
    run_matrix(exact, 64);
    free(exact);

    uint8_t *plus_one = make_mixed(65);
    run_matrix(plus_one, 65);
    free(plus_one);

    /* Large enough to cross many block boundaries at every buffer size,
       delivered in one write since a byte-at-a-time trickle here would be
       5000 read/write round trips per buffer size. */
    uint8_t *large = make_mixed(5000);
    {
        static const size_t buf_sizes[] = {64, 65, 4096, 1024 * 1024};
        for (size_t i = 0; i < sizeof(buf_sizes) / sizeof(buf_sizes[0]); i++) {
            run_case(large, 5000, 0, buf_sizes[i], false);
            run_case(large, 5000, 0, buf_sizes[i], true);
        }
    }
    free(large);

    printf("ok\n");
    return 0;
}

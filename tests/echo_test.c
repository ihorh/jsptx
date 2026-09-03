#define _POSIX_C_SOURCE 200809L

#include "jsp.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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
    char tmpl[] = "/tmp/jsptx_echo_test_XXXXXX";
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

/* Feeds input through a pipe into jsp_run, in chunks of chunk bytes (0 means
   one write), and asserts the bytes that land in a temp file match input
   exactly. */
static void run_case(const uint8_t *input, size_t input_len, size_t chunk, size_t buf_size) {
    jsp_test_writer w = spawn_writer(input, input_len, chunk);
    int             out_fd = open_sink();

    assert(jsp_run(w.read_fd, out_fd, buf_size) == 0);
    close(w.read_fd);
    reap_writer(w.pid);

    assert_sink_equals(out_fd, input, input_len);
    close(out_fd);
}

static uint8_t *make_pattern(size_t len) {
    uint8_t *buf = malloc(len ? len : 1);
    assert(buf != NULL);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(i * 37 + 11);
    }
    return buf;
}

int main(void) {
    /* Empty input. */
    run_case(NULL, 0, 0, 64);

    /* One byte. */
    uint8_t one_byte = 'x';
    run_case(&one_byte, 1, 0, 64);

    /* Larger than the buffer, delivered in one write. */
    size_t   large_len = 4096;
    uint8_t *large_buf = make_pattern(large_len);
    run_case(large_buf, large_len, 0, 64);
    free(large_buf);

    /* Larger than the buffer, delivered in one-byte pipe writes. */
    size_t   trickle_len = 512;
    uint8_t *trickle_buf = make_pattern(trickle_len);
    run_case(trickle_buf, trickle_len, 1, 64);
    free(trickle_buf);

    printf("ok\n");
    return 0;
}

#define _POSIX_C_SOURCE 200809L

/* Feeds the fixtures under tests/data/pluck/ through jsp_run in
   JSP_OUTPUT_PLUCK mode and checks the record stream against each
   fixture's .expected file, and again with --lines against its
   .lines.expected file.

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

/* Each runs with the path in its .path file, or "." when it has none. */
static const char *const FIXTURES[] = {
    "ndjson_objects",
    "array_unwrap",
    "bare_scalars",
    "array_of_scalars",
    "strings",
    "nested",
    "block_boundary",
    "multi_array_unwrap",
    "path_ndjson",
    "path_array_unwrap",
    "path_container",
    "path_key_with_space",
    "path_misses",
    "path_duplicates",
    "path_value_kinds",
    "path_escaped_key",
    "path_scalar_at_block_end",
    "path_long_key",
    "path_value_spans_refill",
    "path_split_key_mismatch",
};

/* The framings jsp_settings_parse picks for a path, one JSON array by
   default and one record per line under --lines. */
static const jsp_framing FRAMING_ARRAY = {
    .open = JSTR("["),
    .separator = JSTR(","),
    .close = JSTR("]\n"),
};
static const jsp_framing FRAMING_LINES = {
    .separator = JSTR("\n"),
    .close = JSTR("\n"),
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
    case JSP_ERR_MALFORMED:
        return "JSP_ERR_MALFORMED";
    case JSP_ERR_LINES_CONTAINER:
        return "JSP_ERR_LINES_CONTAINER";
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

static const jsp_result RESULT_OK = {.status = JSP_OK};

/* Checks one case's jsp_run result, status and offset, and its output against
   want. On a mismatch it reports the case by name, with the first differing
   byte and the bytes around it on both sides, and counts a failure rather
   than aborting, so a run shows every case a change breaks. */
static void check_case(const char *name, jsp_result result, jsp_result want_result,
                       const uint8_t *got, size_t got_len, const uint8_t *want,
                       size_t want_len) {
    cases++;
    bool ok = true;

    if (result.status != want_result.status || result.offset != want_result.offset) {
        fprintf(stderr,
                "FAIL %s: jsp_run returned %s at offset %llu (sys_errno %d), want %s at offset "
                "%llu\n",
                name, status_name(result.status), (unsigned long long)result.offset,
                result.sys_errno, status_name(want_result.status),
                (unsigned long long)want_result.offset);
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

/* Feeds input through a pipe into jsp_run plucking path, one JSON array or
   with lines one record per line, and reads back what it wrote. The caller frees .out. */
static pluck_run
run_pluck(const uint8_t *input, size_t input_len, jstr path, size_t buf_size, bool lines) {
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

    jsp_path parsed;
    bool     path_ok = jsp_path_parse(path, &parsed);
    assert(path_ok);

    int          out_fd = open_sink();
    jsp_fds      fds = {.in_fd = in_pipe[0], .out_fd = out_fd, .trace_fd = -1, .err_fd = -1};
    jsp_settings settings = {.buf_size = buf_size,
                             .output = JSP_OUTPUT_PLUCK,
                             .trace = JSP_TRACE_NONE,
                             .path = parsed,
                             .framing = lines ? FRAMING_LINES : FRAMING_ARRAY,
                             .scalars_only = lines};
    pluck_run    run = {.result = jsp_run(fds, settings)};
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

/* Runs one fixture at one buf_size in one output mode and checks what
   jsp_run wrote against the fixture's .expected file, or .lines.expected
   under lines. A .lines.offset file means lines mode refuses the fixture's
   first container value at that offset, and .lines.expected then holds what
   went out before it. */
static void run_fixture(const char *name, size_t buf_size, bool lines) {
    const char *suffix = lines ? ".lines" : "";
    char        json_path[256];
    char        expected_path[256];
    char        path_path[256];
    char        offset_path[256];
    char        case_name[256];
    snprintf(json_path, sizeof(json_path), "tests/data/pluck/%s.json", name);
    snprintf(expected_path, sizeof(expected_path), "tests/data/pluck/%s%s.expected", name,
             suffix);
    snprintf(path_path, sizeof(path_path), "tests/data/pluck/%s.path", name);
    snprintf(offset_path, sizeof(offset_path), "tests/data/pluck/%s.lines.offset", name);
    snprintf(case_name, sizeof(case_name), "pluck/%s%s buf_size=%zu", name, suffix, buf_size);

    size_t   input_len;
    uint8_t *input = read_file(json_path, &input_len);
    size_t   want_len;
    uint8_t *want = read_file(expected_path, &want_len);
    size_t   path_len = 1;
    uint8_t *path = access(path_path, F_OK) == 0 ? read_file(path_path, &path_len) : NULL;
    jstr     path_str = path ? (jstr){(const char *)path, (ptrdiff_t)path_len} : JSTR(".");

    jsp_result want_result = RESULT_OK;
    if (lines && access(offset_path, F_OK) == 0) {
        size_t   offset_len;
        uint8_t *offset_text = read_file(offset_path, &offset_len);
        want_result.status = JSP_ERR_LINES_CONTAINER;
        want_result.offset = strtoull((const char *)offset_text, NULL, 10);
        free(offset_text);
    }

    pluck_run run = run_pluck(input, input_len, path_str, buf_size, lines);
    check_case(case_name, run.result, want_result, run.out, run.out_len, want, want_len);

    free(input);
    free(path);
    free(want);
    free(run.out);
}

/* Runs input in both output modes at buf_size 64 and checks each against its
   own expected output. */
static void check_both_modes(const char *name, jstr input, jstr want_array, jstr want_lines) {
    char case_name[256];

    pluck_run run =
        run_pluck((const uint8_t *)input.data, (size_t)input.len, JSTR("."), 64, false);
    snprintf(case_name, sizeof(case_name), "%s buf_size=64", name);
    check_case(case_name, run.result, RESULT_OK, run.out, run.out_len,
               (const uint8_t *)want_array.data, (size_t)want_array.len);
    free(run.out);

    run = run_pluck((const uint8_t *)input.data, (size_t)input.len, JSTR("."), 64, true);
    snprintf(case_name, sizeof(case_name), "%s.lines buf_size=64", name);
    check_case(case_name, run.result, RESULT_OK, run.out, run.out_len,
               (const uint8_t *)want_lines.data, (size_t)want_lines.len);
    free(run.out);
}

/* A bare scalar with nothing after it, ending exactly on a 64-byte block
   boundary, so the reader's last block is the full one and the loop breaks
   on JSP_SCAN_END with no further block to trigger the scalar's close. */
static void test_scalar_on_block_boundary(void) {
    uint8_t input[64];
    memset(input, ' ', sizeof(input) - 1);
    input[sizeof(input) - 1] = '7';
    check_both_modes("scalar_on_block_boundary", (jstr){(const char *)input, sizeof(input)},
                     JSTR("[7]\n"), JSTR("7\n"));
}

/* No records at all, from empty input or whitespace alone: the array's
   framing is unconditional in both modes, so under lines the close
   newline stands alone. */
static void test_no_records(void) {
    check_both_modes("empty_input", JSTR(""), JSTR("[]\n"), JSTR("\n"));
    check_both_modes("whitespace_only", JSTR(" \n\t "), JSTR("[]\n"), JSTR("\n"));
    check_both_modes("empty_top_level_array", JSTR("[]"), JSTR("[]\n"), JSTR("\n"));
}

/* Checks that a run at buf_size 64 failed with status at offset, and wrote
   exactly want: what reached out_fd before the failure plus the close framing. */
static void check_failure(const char *name, jstr input, jstr path, bool lines,
                          jsp_status status, uint64_t offset, jstr want) {
    pluck_run  run = run_pluck((const uint8_t *)input.data, (size_t)input.len, path, 64, lines);
    jsp_result want_result = {.status = status, .offset = offset};
    check_case(name, run.result, want_result, run.out, run.out_len, (const uint8_t *)want.data,
               (size_t)want.len);
    free(run.out);
}

/* Depth errors (an unbalanced or mismatched closing bracket, or nesting past
   JSP_MAX_DEPTH) fail the run at the bracket's offset rather than emitting a
   partial record. */
static void test_malformed_input_fails(void) {
    check_failure("unbalanced_close", JSTR("}"), JSTR("."), false, JSP_ERR_MALFORMED, 0,
                  JSTR("[]\n"));
    check_failure("mismatched_close", JSTR("{\"a\":[1,2}"), JSTR("."), false, JSP_ERR_MALFORMED,
                  9, JSTR("[{\"a\":[1,2]\n"));
    check_failure("mismatched_close_past_refill",
                  JSTR("[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,"
                       "17,18,19,20,21,22,23,24,25,26,27}"),
                  JSTR("."), false, JSP_ERR_MALFORMED, 72,
                  JSTR("[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,"
                       "27]\n"));
}

/* Under lines a value that is a container fails the run at its opening
   bracket, before any of its bytes or its separator go out. In array mode
   the same value emits verbatim. */
static void test_lines_refuses_containers(void) {
    check_failure("lines_object", JSTR("{\"a\":{\"b\":1}}"), JSTR(".a"), true,
                  JSP_ERR_LINES_CONTAINER, 5, JSTR("\n"));
    check_failure("lines_array_after_scalar", JSTR("{\"a\":1}\n{\"a\":[]}"), JSTR(".a"), true,
                  JSP_ERR_LINES_CONTAINER, 13, JSTR("1\n"));
    check_failure("lines_whole_record", JSTR("{}"), JSTR("."), true, JSP_ERR_LINES_CONTAINER, 0,
                  JSTR("\n"));
    check_failure("lines_unwrapped_element", JSTR("[1,[2]]"), JSTR("."), true,
                  JSP_ERR_LINES_CONTAINER, 3, JSTR("1\n"));

    pluck_run run = run_pluck((const uint8_t *)"{\"a\":{\"b\":1}}", 13, JSTR(".a"), 64, false);
    check_case("array_mode_emits_container", run.result, RESULT_OK, run.out, run.out_len,
               (const uint8_t *)"[{\"b\":1}]\n", 10);
    free(run.out);
}

int main(void) {
    static const size_t buf_sizes[] = {64, 4096};

    for (size_t i = 0; i < sizeof(FIXTURES) / sizeof(FIXTURES[0]); i++) {
        for (size_t j = 0; j < sizeof(buf_sizes) / sizeof(buf_sizes[0]); j++) {
            run_fixture(FIXTURES[i], buf_sizes[j], false);
            run_fixture(FIXTURES[i], buf_sizes[j], true);
        }
    }

    test_scalar_on_block_boundary();
    test_no_records();
    test_malformed_input_fails();
    test_lines_refuses_containers();

    if (failures > 0) {
        fprintf(stderr, "pluck_test: %d of %d cases failed\n", failures, cases);
        return 1;
    }
    printf("ok\n");
    return 0;
}

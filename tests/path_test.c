/* jsp_path in isolation: what parses, what is rejected, and the segments a
   parsed path hands back. */

#include "jsp_path.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_dot_alone_has_no_segments(void) {
    jsp_path p;
    assert(jsp_path_parse(JSTR("."), &p));
    assert(p.segments == 0);
}

static void test_segments_come_back_in_order(void) {
    jsp_path p;
    assert(jsp_path_parse(JSTR(".user.id"), &p));
    assert(p.segments == 2);
    assert(jstr_equal(jsp_path_segment(&p, 0), JSTR("user")));
    assert(jstr_equal(jsp_path_segment(&p, 1), JSTR("id")));
}

static void test_segment_bytes_stand_for_themselves(void) {
    jsp_path p;
    assert(jsp_path_parse(JSTR(".user name.a\\\"b"), &p));
    assert(p.segments == 2);
    assert(jstr_equal(jsp_path_segment(&p, 0), JSTR("user name")));
    assert(jstr_equal(jsp_path_segment(&p, 1), JSTR("a\\\"b")));
}

static void test_malformed_paths_are_rejected(void) {
    static const char *const bad[] = {"", "user", "a.b", "..", ".a.", ".a..b", "..a"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        jsp_path p;
        assert(!jsp_path_parse(jstr_init(bad[i]), &p));
    }
}

static void test_segment_count_is_bounded(void) {
    char text[2 * (JSP_PATH_MAX_SEGMENTS + 1) + 1];
    for (int i = 0; i < JSP_PATH_MAX_SEGMENTS + 1; i++) {
        text[2 * i] = '.';
        text[2 * i + 1] = 'k';
    }
    jsp_path p;

    jstr at_bound = {text, 2 * JSP_PATH_MAX_SEGMENTS};
    assert(jsp_path_parse(at_bound, &p));
    assert(p.segments == JSP_PATH_MAX_SEGMENTS);
    assert(jstr_equal(jsp_path_segment(&p, JSP_PATH_MAX_SEGMENTS - 1), JSTR("k")));

    jstr past_bound = {text, 2 * (JSP_PATH_MAX_SEGMENTS + 1)};
    assert(!jsp_path_parse(past_bound, &p));
}

int main(void) {
    test_dot_alone_has_no_segments();
    test_segments_come_back_in_order();
    test_segment_bytes_stand_for_themselves();
    test_malformed_paths_are_rejected();
    test_segment_count_is_bounded();

    printf("ok\n");
    return 0;
}

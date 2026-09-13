/* jsp_nesting_step in isolation, no I/O: sequences of structural characters
   fed straight to the stack, checked against the result and the state each
   one should leave behind. */

#include "jsp_nesting.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Feeds chars through state one at a time, asserting each is accepted and
   leaves the depth at the same index in depths. */
static void run_sequence(const char *chars, const unsigned *depths, size_t n) {
    jsp_nesting_state state = {0};
    for (size_t i = 0; i < n; i++) {
        assert(jsp_nesting_step(&state, (uint8_t)chars[i]) == JSP_NESTING_OK);
        assert(jsp_nesting_depth(&state) == depths[i]);
    }
}

static void test_empty_object(void) {
    static const unsigned want[] = {1, 0};
    run_sequence("{}", want, 2);
}

static void test_empty_array(void) {
    static const unsigned want[] = {1, 0};
    run_sequence("[]", want, 2);
}

static void test_nested_mixed(void) {
    /* {[]} : push object, push array, pop array, pop object. */
    static const unsigned want[] = {1, 2, 1, 0};
    run_sequence("{[]}", want, 4);
}

static void test_non_bracket_chars_never_change_depth(void) {
    jsp_nesting_state state = {0};
    assert(jsp_nesting_step(&state, '{') == JSP_NESTING_OK);
    assert(jsp_nesting_step(&state, '[') == JSP_NESTING_OK);
    assert(jsp_nesting_step(&state, '{') == JSP_NESTING_OK);
    jsp_nesting_state before = state;
    static const char chars[] = ":,\"";
    for (size_t i = 0; i < sizeof(chars) - 1; i++) {
        jsp_nesting_result got = jsp_nesting_step(&state, (uint8_t)chars[i]);
        assert(got == JSP_NESTING_OK);
        assert(memcmp(&state, &before, sizeof(state)) == 0);
    }
}

static void test_mismatch(void) {
    /* {] : the open container is an object, ']' wants an array. */
    jsp_nesting_state state = {0};
    assert(jsp_nesting_step(&state, '{') == JSP_NESTING_OK);
    jsp_nesting_state before_error = state;
    assert(jsp_nesting_step(&state, ']') == JSP_NESTING_ERROR_MISMATCH);
    assert(memcmp(&state, &before_error, sizeof(state)) == 0);
}

static void test_unbalanced(void) {
    /* A close bracket with nothing open. */
    jsp_nesting_state state = {0};
    jsp_nesting_state before_error = state;
    assert(jsp_nesting_step(&state, '}') == JSP_NESTING_ERROR_UNBALANCED);
    assert(memcmp(&state, &before_error, sizeof(state)) == 0);

    state = (jsp_nesting_state){0};
    before_error = state;
    assert(jsp_nesting_step(&state, ']') == JSP_NESTING_ERROR_UNBALANCED);
    assert(memcmp(&state, &before_error, sizeof(state)) == 0);
}

static void test_overflow_at_exactly_max_depth(void) {
    jsp_nesting_state state = {0};
    for (unsigned i = 0; i < JSP_MAX_DEPTH; i++) {
        assert(jsp_nesting_step(&state, '[') == JSP_NESTING_OK);
    }
    assert(jsp_nesting_depth(&state) == JSP_MAX_DEPTH);

    jsp_nesting_state before_error = state;
    assert(jsp_nesting_step(&state, '[') == JSP_NESTING_ERROR_OVERFLOW);
    assert(memcmp(&state, &before_error, sizeof(state)) == 0);

    /* Popping back down one level makes room again. */
    assert(jsp_nesting_step(&state, ']') == JSP_NESTING_OK);
    assert(jsp_nesting_depth(&state) == JSP_MAX_DEPTH - 1);
}

static void test_outermost_is_array(void) {
    jsp_nesting_state state = {0};
    assert(!jsp_nesting_outermost_is_array(&state));
    assert(jsp_nesting_step(&state, '[') == JSP_NESTING_OK);
    assert(jsp_nesting_step(&state, '{') == JSP_NESTING_OK);
    assert(jsp_nesting_outermost_is_array(&state));
    assert(jsp_nesting_step(&state, '}') == JSP_NESTING_OK);
    assert(jsp_nesting_step(&state, ']') == JSP_NESTING_OK);

    /* the stale '[' bit left behind must not leak into the next value */
    assert(jsp_nesting_step(&state, '{') == JSP_NESTING_OK);
    assert(!jsp_nesting_outermost_is_array(&state));
}

static void test_concatenated_values_each_return_to_depth_zero(void) {
    /* {}[]{} : three top-level values back to back, with no state surviving
       from one to the next. */
    static const unsigned want[] = {1, 0, 1, 0, 1, 0};
    run_sequence("{}[]{}", want, 6);
}

int main(void) {
    test_empty_object();
    test_empty_array();
    test_nested_mixed();
    test_non_bracket_chars_never_change_depth();
    test_mismatch();
    test_unbalanced();
    test_overflow_at_exactly_max_depth();
    test_outermost_is_array();
    test_concatenated_values_each_return_to_depth_zero();

    printf("ok\n");
    return 0;
}

/* jsp_nesting_step in isolation, no I/O: sequences of structural characters
   fed straight to the stack, checked against the result and the state each
   one should leave behind. */

#include "jsp_nesting.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Feeds chars through state one at a time, asserting each yields the result
   at the same index in results. */
static void run_sequence(const char *chars, const jsp_nesting_result *results, size_t n) {
    jsp_nesting_state state = {0};
    for (size_t i = 0; i < n; i++) {
        jsp_nesting_result got = jsp_nesting_step(&state, (uint8_t)chars[i]);
        assert(got == results[i]);
    }
}

static void test_empty_object(void) {
    static const jsp_nesting_result want[] = {JSP_NESTING_OK, JSP_NESTING_BOUNDARY};
    run_sequence("{}", want, 2);
}

static void test_empty_array(void) {
    static const jsp_nesting_result want[] = {JSP_NESTING_OK, JSP_NESTING_BOUNDARY};
    run_sequence("[]", want, 2);
}

static void test_nested_mixed(void) {
    /* {[]} : push object, push array, pop array (depth still 1), pop object
       (depth 0: the boundary). */
    static const jsp_nesting_result want[] = {JSP_NESTING_OK, JSP_NESTING_OK, JSP_NESTING_OK,
                                              JSP_NESTING_BOUNDARY};
    run_sequence("{[]}", want, 4);
}

static void test_non_bracket_chars_never_change_depth(void) {
    jsp_nesting_state before = {.stack = 0x5, .depth = 3};
    jsp_nesting_state state = before;
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
    assert(state.depth == JSP_MAX_DEPTH);

    jsp_nesting_state before_error = state;
    assert(jsp_nesting_step(&state, '[') == JSP_NESTING_ERROR_OVERFLOW);
    assert(memcmp(&state, &before_error, sizeof(state)) == 0);

    /* Popping back down one level makes room again. */
    assert(jsp_nesting_step(&state, ']') == JSP_NESTING_OK);
    assert(state.depth == JSP_MAX_DEPTH - 1);
}

static void test_concatenated_records_each_reach_boundary(void) {
    /* {}[]{} : three top-level values back to back, each its own boundary,
       with no state surviving from one to the next. */
    static const jsp_nesting_result want[] = {
        JSP_NESTING_OK,       JSP_NESTING_BOUNDARY, JSP_NESTING_OK,
        JSP_NESTING_BOUNDARY, JSP_NESTING_OK,       JSP_NESTING_BOUNDARY,
    };
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
    test_concatenated_records_each_reach_boundary();

    printf("ok\n");
    return 0;
}

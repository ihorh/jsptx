#include "jsp_classify.h"
#include "jsp_string_mask.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Runs one 64-byte block through jsp_classify_masks64 then jsp_string_mask,
   starting from a fresh state, and asserts the result. */
static void check_block(const char *block64, uint64_t want) {
    assert(strlen(block64) == 64);
    jsp_char_masks   masks = jsp_classify_masks64((const uint8_t *)block64);
    jsp_string_state state = {0};
    assert(jsp_string_mask(masks.structural, masks.quote, masks.backslash, &state) == want);
}

/* Fills buf (at least 65 bytes) with text followed by 'x' padding out to 64
   bytes, then a terminating NUL at buf[64] for strlen-based callers. */
static char *pad(char *buf, const char *text) {
    size_t len = strlen(text);
    assert(len <= 64);
    memcpy(buf, text, len);
    memset(buf + len, 'x', 64 - len);
    buf[64] = '\0';
    return buf;
}

/* Structural characters inside a string vanish; the quotes that bound the
   string do not. */
static void check_content_suppressed(void) {
    char buf[65];
    pad(buf, "{\"url\":\"http://x{y}\"}");
    uint64_t want = 0;
    want |= (uint64_t)1 << 0;  /* { */
    want |= (uint64_t)1 << 1;  /* " opening "url" */
    want |= (uint64_t)1 << 5;  /* " closing "url" */
    want |= (uint64_t)1 << 6;  /* : */
    want |= (uint64_t)1 << 7;  /* " opening value */
    want |= (uint64_t)1 << 19; /* " closing value; braces at 16,18 inside it vanish */
    want |= (uint64_t)1 << 20; /* } */
    check_block(buf, want);
}

/* A backslash-escaped quote does not end the string, so nothing between the
   real quotes is structural. */
static void check_escaped_quote_stays_in_string(void) {
    char buf[65];
    pad(buf,
        "\"a\\\"b\"{"); /* "a\"b"{  -> quotes at 0 and 5 are real, byte 3 is an escaped quote */
    uint64_t want = 0;
    want |= (uint64_t)1 << 0; /* " open */
    want |= (uint64_t)1 << 5; /* " close */
    want |= (uint64_t)1 << 6; /* { after the string, not suppressed */
    check_block(buf, want);
}

/* An escaped backslash before a quote leaves the quote real: \\" is an
   escaped backslash followed by a real quote, not an escaped quote. */
static void check_escaped_backslash_leaves_quote_real(void) {
    char buf[65];
    pad(buf, "\"a\\\\\"{"); /* "a\\"{  -> quotes at 0 and 4 are both real */
    uint64_t want = 0;
    want |= (uint64_t)1 << 0; /* " open */
    want |= (uint64_t)1 << 4; /* " close */
    want |= (uint64_t)1 << 5; /* { after the string */
    check_block(buf, want);
}

/* An odd-length backslash run of three still escapes the quote that follows
   it, same as a run of one. */
static void check_odd_backslash_run(void) {
    char buf[65];
    pad(buf, "\"a\\\\\\\"b\"{");
    /* bytes: " a \ \ \ " b " {  (indices 0-8): three backslashes at 2-4 make
       an odd run, so the quote at 5 is escaped and stays in the string; the
       run resets at 'b', so the quote at 7 is real and closes it. */
    uint64_t want = 0;
    want |= (uint64_t)1 << 0; /* " open */
    want |= (uint64_t)1 << 7; /* " close; the escaped quote at 5 is not real */
    want |= (uint64_t)1 << 8; /* { after the string */
    check_block(buf, want);
}

/* State carried across two calls: a string opened in the first block and
   closed in the second must suppress structural characters spanning both,
   and the closing quote must survive even though it lands in block two. */
static void check_carries_across_blocks(void) {
    char block1[65];
    char block2[65];
    /* Block 1: open a string at byte 0, fill the rest with structural
       characters that must all be suppressed since they're inside it. */
    memset(block1, '{', 64);
    block1[0] = '"';
    block1[64] = '\0';

    /* Block 2: still inside the string until byte 3, where it closes; bytes
       0-2 are structural characters that must still be suppressed. */
    char *b2 = pad(block2, "}}}\"{");

    jsp_string_state state = {0};
    jsp_char_masks   m1 = jsp_classify_masks64((const uint8_t *)block1);
    uint64_t         got1 = jsp_string_mask(m1.structural, m1.quote, m1.backslash, &state);
    assert(got1 == ((uint64_t)1 << 0)); /* only the opening quote survives */
    assert(state.in_string == true);

    jsp_char_masks m2 = jsp_classify_masks64((const uint8_t *)b2);
    uint64_t       got2 = jsp_string_mask(m2.structural, m2.quote, m2.backslash, &state);
    uint64_t want2 = ((uint64_t)1 << 3) | ((uint64_t)1 << 4); /* closing " and { after it */
    assert(got2 == want2);
    assert(state.in_string == false);
}

/* A backslash run split across a block boundary: block 1 ends with a single
   backslash, block 2 opens with the quote it escapes. The quote must not
   count as closing the string. */
static void check_backslash_run_carries_across_blocks(void) {
    char block1[65];
    char block2[65];
    memset(block1, 'a', 64);
    block1[0] = '"';
    block1[63] = '\\';
    block1[64] = '\0';

    char *b2 = pad(block2, "\"}\"{"); /* escaped quote, then a real close, then { after */

    jsp_string_state state = {0};
    jsp_char_masks   m1 = jsp_classify_masks64((const uint8_t *)block1);
    jsp_string_mask(m1.structural, m1.quote, m1.backslash, &state);
    assert(state.in_string == true);
    assert(state.in_backslash_run == true);

    jsp_char_masks m2 = jsp_classify_masks64((const uint8_t *)b2);
    uint64_t       got2 = jsp_string_mask(m2.structural, m2.quote, m2.backslash, &state);
    /* byte 0 is the escaped quote (suppressed, not real), byte 1 '}' inside
       the string (suppressed), byte 2 the real closing quote (kept), byte 3
       '{' after the string (kept). */
    uint64_t want2 = ((uint64_t)1 << 2) | ((uint64_t)1 << 3);
    assert(got2 == want2);
    assert(state.in_string == false);
    assert(state.in_backslash_run == false);
}

int main(void) {
    check_content_suppressed();
    check_escaped_quote_stays_in_string();
    check_escaped_backslash_leaves_quote_real();
    check_odd_backslash_run();
    check_carries_across_blocks();
    check_backslash_run_carries_across_blocks();

    printf("ok\n");
    return 0;
}

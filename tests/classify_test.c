#include "jsp_classify.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every position in a 64-byte block, holding one structural character, must
   classify to exactly that bit. 'x' fills the rest since it is never
   structural. */
static void check_directed(void) {
    static const char structural[] = "{}[]:,\"";
    uint8_t           buf[64];

    for (size_t k = 0; k < sizeof(structural) - 1; k++) {
        for (size_t pos = 0; pos < 64; pos++) {
            memset(buf, 'x', sizeof(buf));
            buf[pos] = (uint8_t)structural[k];

            uint64_t want = (uint64_t)1 << pos;
            assert(jsp_classify64_scalar(buf) == want);
            assert(jsp_classify64(buf) == want);
        }
    }
}

/* The scalar version is the oracle: whichever implementation the dispatcher
   picked for this build must agree with it on random input. */
static void check_random(void) {
    srand(12345);
    for (int trial = 0; trial < 1000; trial++) {
        uint8_t buf[64];
        for (size_t i = 0; i < sizeof(buf); i++) {
            buf[i] = (uint8_t)(rand() % 256);
        }

        assert(jsp_classify64(buf) == jsp_classify64_scalar(buf));
    }
}

/* Every position in a 64-byte block, holding one of the eight characters
   jsp_classify_masks64 knows about, must classify to exactly that bit in
   the right one of structural/quote/backslash, and nowhere else. */
static void check_masks_directed(void) {
    static const char structural_only[] = "{}[]:,";
    uint8_t           buf[64];

    for (size_t k = 0; k < sizeof(structural_only) - 1; k++) {
        for (size_t pos = 0; pos < 64; pos++) {
            memset(buf, 'x', sizeof(buf));
            buf[pos] = (uint8_t)structural_only[k];

            uint64_t       want = (uint64_t)1 << pos;
            jsp_char_masks got = jsp_classify_masks64(buf);
            assert(got.structural == want);
            assert(got.quote == 0);
            assert(got.backslash == 0);
        }
    }

    for (size_t pos = 0; pos < 64; pos++) {
        memset(buf, 'x', sizeof(buf));
        buf[pos] = '"';
        uint64_t       want = (uint64_t)1 << pos;
        jsp_char_masks got = jsp_classify_masks64(buf);
        assert(got.structural == want);
        assert(got.quote == want);
        assert(got.backslash == 0);
    }

    for (size_t pos = 0; pos < 64; pos++) {
        memset(buf, 'x', sizeof(buf));
        buf[pos] = '\\';
        jsp_char_masks got = jsp_classify_masks64(buf);
        assert(got.structural == 0);
        assert(got.quote == 0);
        assert(got.backslash == (uint64_t)1 << pos);
    }
}

/* jsp_classify_masks64's structural mask must agree with jsp_classify64's,
   and whichever implementation the dispatcher picked must agree with the
   scalar oracle on all three masks, on random input. */
static void check_masks_random(void) {
    srand(67890);
    for (int trial = 0; trial < 1000; trial++) {
        uint8_t buf[64];
        for (size_t i = 0; i < sizeof(buf); i++) {
            buf[i] = (uint8_t)(rand() % 256);
        }

        jsp_char_masks got = jsp_classify_masks64(buf);
        jsp_char_masks want = jsp_classify_masks64_scalar(buf);
        assert(got.structural == want.structural);
        assert(got.quote == want.quote);
        assert(got.backslash == want.backslash);
        assert(got.structural == jsp_classify64(buf));
    }
}

int main(void) {
    check_directed();
    check_random();
    check_masks_directed();
    check_masks_random();

    printf("ok\n");
    return 0;
}

/* jsp_bits' two primitives, tested on bit patterns rather than on JSON.
   The compositions they serve — escaped quotes, strings spanning a refill —
   are covered end to end by tests/data/strings' fixtures through jsp_run. */

#include "jsp_bits.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

static int failures;

static void check(const char *what, uint64_t got, uint64_t want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got 0x%016" PRIx64 ", want 0x%016" PRIx64 "\n", what, got,
                want);
        failures++;
    }
}

/* The low n bits, so one bit for n of 1 and the whole word for n of 64. */
static void check_low_mask_covers_n_bits(void) {
    check("low 1", jsp_bits_low_mask(1), 0x1);
    check("low 4", jsp_bits_low_mask(4), 0xf);
    check("low 63", jsp_bits_low_mask(63), ~(uint64_t)0 >> 1);
    check("low 64", jsp_bits_low_mask(64), ~(uint64_t)0);
}

/* The lowest set bit's index, and 64 for a word with no bit set, which is the
   case __builtin_ctzll itself leaves undefined. */
static void check_trailing_zeros_counts_low_clear_bits(void) {
    check("tz 0x0", jsp_bits_trailing_zeros(0x0), 64);
    check("tz 0x1", jsp_bits_trailing_zeros(0x1), 0);
    check("tz 0x2", jsp_bits_trailing_zeros(0x2), 1);
    check("tz 0x18", jsp_bits_trailing_zeros(0x18), 3);
    check("tz bit 63", jsp_bits_trailing_zeros((uint64_t)1 << 63), 63);
    check("tz all ones", jsp_bits_trailing_zeros(~(uint64_t)0), 0);
}

/* Within one run of set bits, the 1st, 3rd, 5th bit and so on come back, so a
   lone bit survives, a pair keeps its first, and a triple keeps two. */
static void check_run_parity_counts_from_each_run_start(void) {
    check("parity 0x1", jsp_bits_run_parity(0x1, 0), 0x1);
    check("parity 0x3", jsp_bits_run_parity(0x3, 0), 0x1);
    check("parity 0x7", jsp_bits_run_parity(0x7, 0), 0x5);
    check("parity 0xf", jsp_bits_run_parity(0xf, 0), 0x5);
    check("parity 0x0", jsp_bits_run_parity(0x0, 0), 0x0);
}

/* Parity counts from a run's own start, not from bit 0, so the same run
   shifted up keeps the same shape. */
static void check_run_parity_ignores_absolute_position(void) {
    check("parity 0x6", jsp_bits_run_parity(0x6, 0), 0x2);
    check("parity 0xe", jsp_bits_run_parity(0xe, 0), 0xa);
    check("parity 0x18", jsp_bits_run_parity(0x18, 0), 0x8);
}

/* starts_inverted flips the run that begins at bit 0, which is how a run
   continuing from the previous word comes out right. It leaves a word whose
   bit 0 is clear untouched. */
static void check_run_parity_inverts_a_continuing_run(void) {
    check("parity 0x1 inverted", jsp_bits_run_parity(0x1, 1), 0x0);
    check("parity 0x3 inverted", jsp_bits_run_parity(0x3, 1), 0x2);
    check("parity 0x7 inverted", jsp_bits_run_parity(0x7, 1), 0x2);
    check("parity all ones inverted", jsp_bits_run_parity(~(uint64_t)0, 1), JSP_ODD_BITS);
    check("parity 0x6 inverted", jsp_bits_run_parity(0x6, 1), 0x2);
}

/* One toggle turns the region on for the rest of the word; a second turns it
   back off, and the bit that turns it off is itself outside. */
static void check_prefix_xor_spans_between_toggles(void) {
    check("xor 0x0", jsp_bits_prefix_xor(0x0, 0), 0x0);
    check("xor 0x1", jsp_bits_prefix_xor(0x1, 0), ~(uint64_t)0);
    check("xor 0x9", jsp_bits_prefix_xor(0x9, 0), 0x7);
    check("xor 0x12", jsp_bits_prefix_xor(0x12, 0), 0xe);
}

/* starts_on carries an already-open region in from the previous word, so the
   first toggle closes it instead of opening one. */
static void check_prefix_xor_carries_an_open_region(void) {
    check("xor 0x0 carried", jsp_bits_prefix_xor(0x0, 1), ~(uint64_t)0);
    check("xor 0x1 carried", jsp_bits_prefix_xor(0x1, 1), 0x0);
    check("xor 0x8 carried", jsp_bits_prefix_xor(0x8, 1), 0x7);
}

int main(void) {
    check_low_mask_covers_n_bits();
    check_trailing_zeros_counts_low_clear_bits();
    check_run_parity_counts_from_each_run_start();
    check_run_parity_ignores_absolute_position();
    check_run_parity_inverts_a_continuing_run();
    check_prefix_xor_spans_between_toggles();
    check_prefix_xor_carries_an_open_region();

    if (failures > 0) {
        fprintf(stderr, "bits_test: %d checks failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}

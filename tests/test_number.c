#include "rubraview/number.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    printf("[test_number] Starting number parsing unit tests...\n");
    double d = 0.0;
    int64_t i = 0;
    size_t used = 0;

    /* Whole-view numbers, with the spaces an INI value may keep. */
    assert(rubraview_parse_double(U8("1.5"), &d) && d == 1.5);
    assert(rubraview_parse_double(U8(" -0.25\t"), &d) && d == -0.25);
    assert(rubraview_parse_double(U8("1e3"), &d) && d == 1000.0);
    assert(rubraview_parse_double(U8("0.1"), &d) && d == 0.1);   /* correctly rounded */
    assert(!rubraview_parse_double(U8(""), &d));
    assert(!rubraview_parse_double(U8("   "), &d));
    assert(!rubraview_parse_double(U8("1.5x"), &d));
    assert(!rubraview_parse_double(U8("abc"), &d));
    printf("  [PASS] A whole view is one number, spaces around it allowed\n");

    /* Nothing that is not a finite number: a setting of NaN or infinity
       would poison every comparison it took part in. */
    assert(!rubraview_parse_double(U8("nan"), &d));
    assert(!rubraview_parse_double(U8("inf"), &d));
    assert(!rubraview_parse_double(U8("-infinity"), &d));
    assert(!rubraview_parse_double(U8("1e999"), &d));
    printf("  [PASS] Infinity, NaN and overflow are refused\n");

    /* The view is read where it lies and not a byte further: here the
       digits run on past the view's end in memory. */
    const char *longer = "12345";
    assert(rubraview_parse_double((u8str_t){ .ptr = longer, .len = 2 }, &d) && d == 12.0);
    assert(rubraview_parse_i64((u8str_t){ .ptr = longer, .len = 3 }, &i) && i == 123);
    /* Longer than any stack buffer the old code copied into. */
    assert(rubraview_parse_double(U8("0.000000000000000000000000000000000000000000000000000000000000000000000001"), &d) &&
           d == 1e-72);
    printf("  [PASS] A view is read within its length, however long\n");

    /* A number at the start, with its unit left for the caller. */
    assert(rubraview_parse_double_prefix(U8("-7.23 dB"), &d, &used) && d == -7.23 && used == 5);
    assert(rubraview_parse_double_prefix(U8("  500ms"), &d, &used) && d == 500.0 && used == 5);
    assert(rubraview_parse_double_prefix(U8("42"), &d, NULL) && d == 42.0);
    assert(!rubraview_parse_double_prefix(U8("dB"), &d, &used));
    printf("  [PASS] A number at the start of a view reports what it used\n");

    /* Integers. */
    assert(rubraview_parse_i64(U8("0"), &i) && i == 0);
    assert(rubraview_parse_i64(U8(" -17 "), &i) && i == -17);
    assert(rubraview_parse_i64(U8("9223372036854775807"), &i) && i == INT64_MAX);
    assert(!rubraview_parse_i64(U8("9223372036854775808"), &i));
    assert(!rubraview_parse_i64(U8("1.5"), &i));
    assert(!rubraview_parse_i64(U8("12a"), &i));
    assert(!rubraview_parse_i64(U8(""), &i));
    printf("  [PASS] An integer is whole, decimal and in range\n");

    printf("[test_number] All tests passed successfully!\n");
    return 0;
}

#include "codegen/out.h"
#include "test.h"

#include <string.h>

static void test_fixed_array_truncates(void) {
    char small[8];
    fmt_into(small, sizeof small, "%s-%d", "truncated", 42);
    CHECK(strcmp(small, "truncat") == 0);
    Out out;
    char array[6];
    out_to_array(&out, array, sizeof array);
    out_fmt(&out, "%d", 123);
    out_fmt(&out, "%d", 4567);   // only two of these fit
    CHECK(strcmp(array, "12345") == 0);
}

int main(void) {
    test_fixed_array_truncates();
    return test_report("out");
}

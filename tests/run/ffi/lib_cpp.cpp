// The C++ side of lib_cpp.of: the generated C header from C++.
#include <cstdio>
#include <vector>

#include "lib_cpp.h"

int main() {
    if (stats_version() != 1) {
        std::printf("unexpected version %d\n", stats_version());
        return 1;
    }
    Stats s;
    stats_init(&s);
    std::vector<int64_t> values = {4, 5, 6};
    for (int64_t v : values) {
        stats_add(&s, v);
    }
    stats_fill(&s, 3, [](int32_t i) -> int64_t { return int64_t{i} * 10; });
    std::printf("C++ sees %lld values\n", static_cast<long long>(s.count));
    std::fflush(stdout);
    stats_report(&s);
    ofir_stdout_flush();
    return 0;
}

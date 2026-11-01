// SPDX-License-Identifier: MIT
#include <cstdio>

#include <lfc/lfc.hpp>

int main() {
    lfc::SpscRing<int> ring(4);
    if (!ring.try_push(42)) {
        return 1;
    }
    const auto value = ring.try_pop();
    std::printf("lfc %s: popped %d\n", LFC_VERSION_STRING, value.value_or(-1));
    return value == 42 ? 0 : 1;
}

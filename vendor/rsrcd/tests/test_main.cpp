// rsrcd test main
#include "rsrcd.hpp"

#include <cstdint>
#include <cstdio>

int main() {
    const uint8_t bytes[] = {0x12, 0x34};
    if (rsrcd::read_u16be(bytes) != 0x1234) {
        return 1;
    }
    std::printf("rsrcd tests placeholder\n");
    return 0;
}

#include "../App/monotonic_micros.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main()
{
    assert(compose_micros_snapshot(10, 120, false, 121) == 10121);
    assert(compose_micros_snapshot(10, 3, true, 4) == 11004);
    assert(compose_micros_snapshot(10, 999, false, 0) == 11000);
    assert(compose_micros_snapshot(10, 998, false, 999) == 10999);
    assert(compose_micros_snapshot(
               std::numeric_limits<std::uint32_t>::max(), 999, false, 0) ==
           4294967296000ULL);
}

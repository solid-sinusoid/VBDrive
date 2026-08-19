#include "../Drivers/libcxxcanard/cyphal/providers/rx_priority.hpp"

#include <cassert>

int main()
{
    const auto order = cyphal::fdcan::receive_fifo_priority();
    assert(order.size() == 2U);
    assert(order[0] == cyphal::fdcan::ReceiveFifo::Critical);
    assert(order[1] == cyphal::fdcan::ReceiveFifo::Regular);
}

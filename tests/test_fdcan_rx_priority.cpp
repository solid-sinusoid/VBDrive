#include "../Drivers/libcxxcanard/cyphal/providers/rx_priority.hpp"

#include <cassert>

int main()
{
    const auto order = cyphal::fdcan::receive_fifo_priority();
    assert(order.size() == 2U);
    assert(order[0] == cyphal::fdcan::ReceiveFifo::Critical);
    assert(order[1] == cyphal::fdcan::ReceiveFifo::Regular);
    assert(cyphal::fdcan::maximum_frames_per_pass(
               cyphal::fdcan::ReceiveFifo::Critical) == 1U);
    assert(cyphal::fdcan::maximum_frames_per_pass(
               cyphal::fdcan::ReceiveFifo::Regular) == 1U);
}

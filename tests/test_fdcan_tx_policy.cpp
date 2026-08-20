#include "../App/fdcan_tx_policy.hpp"
#include "../App/fdcan_timing.hpp"
#include "../Drivers/libcxxcanard/cyphal/providers/tx_event_policy.hpp"

#include <cassert>

int main()
{
    using vbdrive::fdcan::should_preempt_for_critical_status;

    assert(!should_preempt_for_critical_status(0U, 0U));
    assert(!should_preempt_for_critical_status(0x1U, 1U));
    assert(should_preempt_for_critical_status(0x7U, 0U));

    // Runtime does not consume the STM32 TX event FIFO.  Enabling event
    // storage would therefore fill its three slots and continuously lose
    // events under normal state telemetry.
    assert(!cyphal::fdcan::store_tx_events());

    using vbdrive::fdcan::tx_delay_compensation_offset_tq;
    assert(tx_delay_compensation_offset_tq(5U, 1U, 0U) == 5U);
    assert(tx_delay_compensation_offset_tq(5U, 1U, 3U) == 3U);
}

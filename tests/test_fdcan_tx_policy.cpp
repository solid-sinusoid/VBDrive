#include "../App/fdcan_tx_policy.hpp"
#include "../App/fdcan_timing.hpp"

#include <cassert>

int main()
{
    using vbdrive::fdcan::should_preempt_for_critical_status;

    assert(!should_preempt_for_critical_status(0U, 0U));
    assert(!should_preempt_for_critical_status(0x1U, 1U));
    assert(should_preempt_for_critical_status(0x7U, 0U));

    using vbdrive::fdcan::tx_delay_compensation_offset_tq;
    assert(tx_delay_compensation_offset_tq(5U, 1U, 0U) == 5U);
    assert(tx_delay_compensation_offset_tq(5U, 1U, 3U) == 3U);
}

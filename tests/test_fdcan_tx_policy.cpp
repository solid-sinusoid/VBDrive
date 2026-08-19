#include "../App/fdcan_tx_policy.hpp"

#include <cassert>

int main()
{
    using vbdrive::fdcan::should_preempt_for_critical_status;

    assert(!should_preempt_for_critical_status(0U, 0U));
    assert(!should_preempt_for_critical_status(0x1U, 1U));
    assert(should_preempt_for_critical_status(0x7U, 0U));
}

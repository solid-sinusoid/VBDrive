#pragma once

#include <cstdint>

namespace vbdrive::fdcan {

constexpr bool should_preempt_for_critical_status(
    const std::uint32_t pending_tx_mask,
    const std::uint32_t free_tx_slots) noexcept
{
    return pending_tx_mask != 0U && free_tx_slots == 0U;
}

}  // namespace vbdrive::fdcan

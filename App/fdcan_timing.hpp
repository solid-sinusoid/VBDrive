#pragma once

#include <cstdint>

namespace vbdrive::fdcan {

constexpr std::uint32_t tx_delay_compensation_offset_tq(
    const std::uint32_t data_time_seg1,
    const std::uint32_t data_prescaler,
    const std::uint32_t override_tq) noexcept
{
    return override_tq != 0U ? override_tq : data_time_seg1 * data_prescaler;
}

}  // namespace vbdrive::fdcan

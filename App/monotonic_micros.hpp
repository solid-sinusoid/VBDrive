#pragma once

#include <cstdint>

constexpr std::uint64_t compose_micros_snapshot(
    const std::uint64_t millis,
    const std::uint32_t counter_before,
    const bool update_pending,
    const std::uint32_t counter_after)
{
    const bool wrapped = update_pending || (counter_after < counter_before);
    return (millis + (wrapped ? 1U : 0U)) * 1000U + counter_after;
}

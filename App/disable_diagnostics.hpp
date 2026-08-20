#pragma once

#include <atomic>
#include <cstdint>

enum class MotorDisableReason : std::uint32_t {
    None = 0,
    RegisterStop = 1,
    SyncWatchdog = 2,
    InvalidFocPoint = 3,
    StateManagerStop = 4,
};

struct FdcanDiagnosticSnapshot {
    std::uint8_t tx_error_count{};
    std::uint8_t rx_error_count{};
    std::uint8_t error_logging_count{};
    std::uint8_t last_error_code{};
    std::uint8_t data_last_error_code{};
    bool bus_off{};
    bool error_passive{};
    bool warning{};
    bool protocol_exception{};
    bool rx_error_passive{};
};

constexpr std::uint32_t pack_fdcan_diagnostics(const FdcanDiagnosticSnapshot& snapshot) noexcept
{
    std::uint32_t value = snapshot.tx_error_count;
    value |= static_cast<std::uint32_t>(snapshot.rx_error_count) << 8U;
    value |= static_cast<std::uint32_t>(snapshot.error_logging_count) << 16U;
    value |= static_cast<std::uint32_t>(snapshot.last_error_code & 0x07U) << 24U;
    value |= static_cast<std::uint32_t>(snapshot.data_last_error_code & 0x07U) << 27U;
    value |= static_cast<std::uint32_t>(snapshot.bus_off) << 30U;
    value |= static_cast<std::uint32_t>(snapshot.error_passive) << 31U;
    return value;
}

struct FdcanRxFifoDiagnosticSnapshot {
    std::uint8_t fifo0_fill_level{};
    bool fifo0_full{};
    bool fifo0_lost{};
    std::uint8_t fifo1_fill_level{};
    bool fifo1_full{};
    bool fifo1_lost{};
};

// FIFO0 and FIFO1 on STM32G431 each hold at most three FD frames.  Preserve
// the current fill level and sticky overflow evidence in an independent
// read-only register, so a lost FOC command can be distinguished from a bus
// protocol error.
constexpr std::uint32_t pack_fdcan_rx_fifo_diagnostics(
    const FdcanRxFifoDiagnosticSnapshot& snapshot) noexcept
{
    std::uint32_t value = static_cast<std::uint32_t>(snapshot.fifo0_fill_level & 0x0FU);
    value |= static_cast<std::uint32_t>(snapshot.fifo0_full) << 3U;
    value |= static_cast<std::uint32_t>(snapshot.fifo0_lost) << 4U;
    value |= static_cast<std::uint32_t>(snapshot.fifo1_fill_level & 0x0FU) << 8U;
    value |= static_cast<std::uint32_t>(snapshot.fifo1_full) << 11U;
    value |= static_cast<std::uint32_t>(snapshot.fifo1_lost) << 12U;
    return value;
}

class MotorDisableDiagnostics {
public:
    void record(const MotorDisableReason reason) noexcept
    {
        reason_.store(static_cast<std::uint32_t>(reason), std::memory_order_release);
        count_.fetch_add(1U, std::memory_order_relaxed);
    }

    MotorDisableReason reason() const noexcept
    {
        return static_cast<MotorDisableReason>(reason_.load(std::memory_order_acquire));
    }

    std::uint32_t count() const noexcept
    {
        return count_.load(std::memory_order_relaxed);
    }

    std::uint32_t packed_state() const noexcept
    {
        return (static_cast<std::uint32_t>(reason()) & 0xFFU) | (count() << 8U);
    }

private:
    std::atomic<std::uint32_t> reason_{static_cast<std::uint32_t>(MotorDisableReason::None)};
    std::atomic<std::uint32_t> count_{0U};
};

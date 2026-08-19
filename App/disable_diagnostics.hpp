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

private:
    std::atomic<std::uint32_t> reason_{static_cast<std::uint32_t>(MotorDisableReason::None)};
    std::atomic<std::uint32_t> count_{0U};
};

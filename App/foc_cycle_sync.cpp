#include "foc_cycle_sync.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

FocCycleSync::FocCycleSync(
    const SyncMode mode,
    const std::uint32_t control_period_us,
    const std::uint32_t watchdog_us)
    : mode_(mode),
      control_period_us_(control_period_us),
      watchdog_us_(watchdog_us)
{
}

void FocCycleSync::push_main_status(const CommandStatus status)
{
    const auto next = static_cast<std::uint8_t>((main_status_head_ + 1U) % status_capacity);
    if (next == main_status_tail_) {
        return;
    }
    main_statuses_[main_status_head_] = status;
    main_status_head_ = next;
}

void FocCycleSync::publish_applied_from_isr(const CommandStatus status)
{
    if (applied_mailbox_.valid.load(std::memory_order_acquire)) {
        return;
    }
    applied_mailbox_.status = status;
    applied_mailbox_.valid.store(true, std::memory_order_release);
}

FocCycleSync::Slot* FocCycleSync::find_slot(
    const std::uint16_t cycle_id,
    const SlotState required_state)
{
    for (auto& slot : slots_) {
        if ((slot.state.load(std::memory_order_acquire) == required_state) &&
            (slot.command.cycle_id == cycle_id)) {
            return &slot;
        }
    }
    return nullptr;
}

const FocCycleSync::Slot* FocCycleSync::find_slot(
    const std::uint16_t cycle_id,
    const SlotState required_state) const
{
    for (const auto& slot : slots_) {
        if ((slot.state.load(std::memory_order_acquire) == required_state) &&
            (slot.command.cycle_id == cycle_id)) {
            return &slot;
        }
    }
    return nullptr;
}

StageResult FocCycleSync::stage(
    const CycleCommand& command,
    const bool target_valid,
    const std::uint64_t rx_us)
{
    if (!target_valid) {
        push_main_status({command.cycle_id, StatusCode::Rejected, StatusReason::InvalidNumber, 0});
        return StageResult::Rejected;
    }

    if (has_last_applied_.load(std::memory_order_acquire)) {
        const auto last = last_applied_cycle_.load(std::memory_order_relaxed);
        if (!cycle_after(command.cycle_id, last)) {
            push_main_status({command.cycle_id, StatusCode::Rejected, StatusReason::Stale, 0});
            return StageResult::Rejected;
        }
        if (static_cast<std::uint16_t>(command.cycle_id - last) > slots_.size()) {
            push_main_status({command.cycle_id, StatusCode::Rejected, StatusReason::TooFarAhead, 0});
            return StageResult::Rejected;
        }
    }

    if ((find_slot(command.cycle_id, SlotState::Staged) != nullptr) ||
        (find_slot(command.cycle_id, SlotState::Armed) != nullptr)) {
        push_main_status({command.cycle_id, StatusCode::Staged, StatusReason::None, 0});
        return StageResult::Staged;
    }

    if ((mode_ == SyncMode::Immediate) &&
        (armed_slot_.load(std::memory_order_acquire) != no_slot)) {
        push_main_status({command.cycle_id, StatusCode::Rejected, StatusReason::NoFreeSlot, 0});
        return StageResult::Rejected;
    }

    for (std::uint8_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        auto expected = SlotState::Empty;
        if (!slot.state.compare_exchange_strong(
                expected, SlotState::Writing, std::memory_order_acq_rel)) {
            continue;
        }
        slot.command = command;
        slot.marker_us = rx_us;
        slot.state.store(SlotState::Staged, std::memory_order_release);
        push_main_status({command.cycle_id, StatusCode::Staged, StatusReason::None, 0});
        if (mode_ == SyncMode::Immediate) {
            slot.state.store(SlotState::Armed, std::memory_order_release);
            armed_slot_.store(index, std::memory_order_release);
        }
        return StageResult::Staged;
    }

    push_main_status({command.cycle_id, StatusCode::Rejected, StatusReason::NoFreeSlot, 0});
    return StageResult::Rejected;
}

SyncResult FocCycleSync::on_sync(const std::uint16_t cycle_id, const std::uint64_t rx_us)
{
    if (mode_ != SyncMode::Synchronized) {
        return SyncResult::Ignored;
    }
    if (has_last_applied_.load(std::memory_order_acquire) &&
        (last_applied_cycle_.load(std::memory_order_relaxed) == cycle_id)) {
        return SyncResult::Ignored;
    }
    if (find_slot(cycle_id, SlotState::Armed) != nullptr) {
        return SyncResult::Ignored;
    }

    for (std::uint8_t index = 0; index < slots_.size(); ++index) {
        auto& slot = slots_[index];
        if ((slot.state.load(std::memory_order_acquire) != SlotState::Staged) ||
            (slot.command.cycle_id != cycle_id)) {
            continue;
        }
        if (armed_slot_.load(std::memory_order_acquire) != no_slot) {
            push_main_status({cycle_id, StatusCode::Rejected, StatusReason::NoFreeSlot, 0});
            return SyncResult::Rejected;
        }
        slot.marker_us = rx_us;
        slot.state.store(SlotState::Armed, std::memory_order_release);
        armed_slot_.store(index, std::memory_order_release);
        last_sync_us_ = rx_us;
        has_last_sync_ = true;
        watchdog_reported_ = false;
        return SyncResult::Armed;
    }

    push_main_status({cycle_id, StatusCode::Rejected, StatusReason::NoMatchingCommand, 0});
    return SyncResult::Rejected;
}

std::int32_t FocCycleSync::saturated_offset(
    const std::uint64_t apply_us,
    const std::uint64_t marker_us)
{
    const auto positive_limit = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (apply_us >= marker_us) {
        return static_cast<std::int32_t>(std::min(apply_us - marker_us, positive_limit));
    }
    const auto magnitude = std::min(
        marker_us - apply_us,
        positive_limit + 1U);
    if (magnitude == (positive_limit + 1U)) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return -static_cast<std::int32_t>(magnitude);
}

std::optional<AppliedCycle> FocCycleSync::consume_armed(const std::uint64_t apply_us)
{
    const auto index = armed_slot_.exchange(no_slot, std::memory_order_acq_rel);
    if (index >= slots_.size()) {
        return std::nullopt;
    }

    auto& slot = slots_[index];
    if (slot.state.load(std::memory_order_acquire) != SlotState::Armed) {
        return std::nullopt;
    }
    const auto command = slot.command;
    const auto offset = saturated_offset(apply_us, slot.marker_us);
    last_applied_cycle_.store(command.cycle_id, std::memory_order_relaxed);
    has_last_applied_.store(true, std::memory_order_release);
    slot.state.store(SlotState::Empty, std::memory_order_release);

    if (mode_ == SyncMode::Synchronized) {
        publish_applied_from_isr(
            {command.cycle_id, StatusCode::Applied, StatusReason::None, offset});
    } else {
        immediate_apply_.cycle_id = command.cycle_id;
        immediate_apply_.apply_us = apply_us;
        immediate_apply_.valid.store(true, std::memory_order_release);
    }
    return AppliedCycle{command, offset};
}

bool FocCycleSync::immediate_marker(
    const std::uint16_t cycle_id,
    const std::uint64_t marker_us)
{
    if ((mode_ != SyncMode::Immediate) ||
        !immediate_apply_.valid.load(std::memory_order_acquire) ||
        (immediate_apply_.cycle_id != cycle_id)) {
        return false;
    }
    const auto offset = saturated_offset(immediate_apply_.apply_us, marker_us);
    immediate_apply_.valid.store(false, std::memory_order_release);
    push_main_status({cycle_id, StatusCode::Applied, StatusReason::None, offset});
    return true;
}

WatchdogAction FocCycleSync::poll_watchdog(const std::uint64_t now_us)
{
    if ((mode_ != SyncMode::Synchronized) || !has_last_sync_ || (now_us < last_sync_us_)) {
        return WatchdogAction::None;
    }
    const auto elapsed = now_us - last_sync_us_;
    if (elapsed < control_period_us_) {
        return WatchdogAction::None;
    }
    if (elapsed < watchdog_us_) {
        return WatchdogAction::Hold;
    }
    if (!watchdog_reported_) {
        watchdog_reported_ = true;
        push_main_status({
            last_applied_cycle_.load(std::memory_order_relaxed),
            StatusCode::Timeout,
            StatusReason::Watchdog,
            0});
    }
    return WatchdogAction::Disable;
}

std::optional<CommandStatus> FocCycleSync::pop_status()
{
    if (main_status_tail_ != main_status_head_) {
        const auto status = main_statuses_[main_status_tail_];
        main_status_tail_ = static_cast<std::uint8_t>((main_status_tail_ + 1U) % status_capacity);
        return status;
    }
    if (!applied_mailbox_.valid.load(std::memory_order_acquire)) {
        return std::nullopt;
    }
    const auto status = applied_mailbox_.status;
    applied_mailbox_.valid.store(false, std::memory_order_release);
    return status;
}

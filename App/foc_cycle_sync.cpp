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

void FocCycleSync::reset_session()
{
    armed_slot_.store(no_slot, std::memory_order_release);
    for (auto& slot : slots_) {
        slot.state.store(SlotState::Empty, std::memory_order_release);
    }
    applied_mailbox_.valid.store(false, std::memory_order_release);
    immediate_apply_.valid.store(false, std::memory_order_release);
    last_apply_offset_microsecond_.store(0, std::memory_order_relaxed);
    last_applied_phase_.store(
        static_cast<std::uint8_t>(SyncPhase::Run),
        std::memory_order_relaxed);
    has_last_applied_.store(false, std::memory_order_release);
    has_applying_cycle_.store(false, std::memory_order_release);
    has_last_sync_ = false;
    watchdog_reported_ = false;
    session_phase_ = SessionPhase::Idle;
    main_status_head_ = 0;
    main_status_tail_ = 0;
}

void FocCycleSync::set_mode(const SyncMode mode)
{
    reset_session();
    mode_ = mode;
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

SyncResult FocCycleSync::on_sync(
    const std::uint16_t cycle_id,
    const SyncPhase phase,
    const std::uint64_t rx_us)
{
    if ((phase != SyncPhase::Prepare) && (phase != SyncPhase::Run)) {
        push_main_status({cycle_id, StatusCode::Rejected, StatusReason::OutOfRange, 0});
        return SyncResult::Rejected;
    }
    if (mode_ != SyncMode::Synchronized) {
        return SyncResult::Ignored;
    }
    if ((session_phase_ == SessionPhase::Run) && (phase == SyncPhase::Prepare)) {
        push_main_status({cycle_id, StatusCode::Rejected, StatusReason::OutOfRange, 0});
        return SyncResult::Rejected;
    }
    if (has_last_applied_.load(std::memory_order_acquire) &&
        (last_applied_cycle_.load(std::memory_order_relaxed) == cycle_id) &&
        (last_applied_phase_.load(std::memory_order_relaxed) ==
         static_cast<std::uint8_t>(phase))) {
        // The master repeats idempotent RUN markers until it receives APPLIED.
        // Keep the watchdog alive while a delayed main loop publishes it.
        last_sync_us_ = rx_us;
        has_last_sync_ = true;
        watchdog_reported_ = false;
        push_main_status({
            cycle_id,
            StatusCode::Applied,
            StatusReason::None,
            last_apply_offset_microsecond_.load(std::memory_order_relaxed)});
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
        slot.phase = phase;
        slot.state.store(SlotState::Armed, std::memory_order_release);
        armed_slot_.store(index, std::memory_order_release);
        session_phase_ =
            (phase == SyncPhase::Run) ? SessionPhase::Run : SessionPhase::Prepare;
        if (phase == SyncPhase::Run) {
            run_armed_count_.fetch_add(1U, std::memory_order_relaxed);
        }
        last_sync_us_ = rx_us;
        has_last_sync_ = true;
        watchdog_reported_ = false;
        return SyncResult::Armed;
    }

    if (phase == SyncPhase::Prepare) {
        if (session_phase_ == SessionPhase::Prepare) {
            last_sync_us_ = rx_us;
            has_last_sync_ = true;
            watchdog_reported_ = false;
        }
        return SyncResult::Ignored;
    }
    // consume_armed() clears the slot before the FOC ISR completes the output.
    // A repeated RUN marker in this interval belongs to that in-flight apply;
    // treating it as unmatched races the ISR and produces a false rejection.
    if (has_applying_cycle_.load(std::memory_order_acquire) &&
        (applying_cycle_.load(std::memory_order_relaxed) == cycle_id) &&
        (applying_phase_.load(std::memory_order_relaxed) ==
         static_cast<std::uint8_t>(phase))) {
        last_sync_us_ = rx_us;
        has_last_sync_ = true;
        watchdog_reported_ = false;
        return SyncResult::Ignored;
    }

    run_rejected_count_.fetch_add(1U, std::memory_order_relaxed);
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
    const auto phase = slot.phase;
    const auto offset = saturated_offset(apply_us, slot.marker_us);
    slot.state.store(SlotState::Empty, std::memory_order_release);
    applying_cycle_.store(command.cycle_id, std::memory_order_relaxed);
    applying_phase_.store(static_cast<std::uint8_t>(phase), std::memory_order_relaxed);
    has_applying_cycle_.store(true, std::memory_order_release);
    if (phase == SyncPhase::Run) {
        run_consumed_count_.fetch_add(1U, std::memory_order_relaxed);
    }
    return AppliedCycle{command, phase, offset, apply_us};
}

void FocCycleSync::complete_apply(const AppliedCycle& applied, const bool accepted)
{
    if (!accepted) {
        if (applied.phase == SyncPhase::Run) {
            run_rejected_count_.fetch_add(1U, std::memory_order_relaxed);
        }
        publish_applied_from_isr({
            applied.command.cycle_id,
            StatusCode::Rejected,
            StatusReason::HardwareFault,
            applied.apply_offset_microsecond});
        has_applying_cycle_.store(false, std::memory_order_release);
        return;
    }
    last_apply_offset_microsecond_.store(
        applied.apply_offset_microsecond,
        std::memory_order_relaxed);
    last_applied_cycle_.store(applied.command.cycle_id, std::memory_order_relaxed);
    last_applied_phase_.store(
        static_cast<std::uint8_t>(applied.phase),
        std::memory_order_relaxed);
    has_last_applied_.store(true, std::memory_order_release);
    has_applying_cycle_.store(false, std::memory_order_release);
    if (applied.phase == SyncPhase::Run) {
        run_completed_count_.fetch_add(1U, std::memory_order_relaxed);
    }
    if (mode_ == SyncMode::Synchronized) {
        publish_applied_from_isr(
            {applied.command.cycle_id,
             StatusCode::Applied,
             StatusReason::None,
             applied.apply_offset_microsecond});
    } else {
        immediate_apply_.cycle_id = applied.command.cycle_id;
        immediate_apply_.apply_us = applied.apply_timestamp_us;
        immediate_apply_.valid.store(true, std::memory_order_release);
    }
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
    const auto active_watchdog_us =
        (session_phase_ == SessionPhase::Prepare) ? prepare_watchdog_us : watchdog_us_;
    if (elapsed < control_period_us_) {
        return WatchdogAction::None;
    }
    if (elapsed < active_watchdog_us) {
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

std::uint32_t FocCycleSync::run_progress() const
{
    return static_cast<std::uint32_t>(run_armed_count_.load(std::memory_order_relaxed)) |
           (static_cast<std::uint32_t>(run_consumed_count_.load(std::memory_order_relaxed)) << 8U) |
           (static_cast<std::uint32_t>(run_completed_count_.load(std::memory_order_relaxed)) << 16U) |
           (static_cast<std::uint32_t>(run_rejected_count_.load(std::memory_order_relaxed)) << 24U);
}

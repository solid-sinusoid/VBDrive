#include "../App/foc_cycle_sync.hpp"

#include <cassert>
#include <cstdint>

namespace {

CycleCommand command(const std::uint16_t cycle_id)
{
    return CycleCommand{
        .cycle_id = cycle_id,
        .target = {.torque = 0.1F, .angle = 1.0F, .velocity = 0.0F,
                   .angle_kp = 7.0F, .velocity_kp = 0.5F},
        .current_kp = 16.0F,
        .current_ki = 0.6F,
    };
}

CommandStatus require_status(FocCycleSync& sync)
{
    const auto status = sync.pop_status();
    assert(status.has_value());
    return *status;
}

void test_staged_sync_applied_once()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(42), true, 900) == StageResult::Staged);
    assert(require_status(sync).status == StatusCode::Staged);
    assert(sync.on_sync(42, 1000) == SyncResult::Armed);

    const auto applied = sync.consume_armed(1020);
    assert(applied.has_value());
    assert(applied->command.cycle_id == 42);
    assert(applied->apply_offset_microsecond == 20);
    assert(!sync.consume_armed(1021).has_value());

    const auto status = require_status(sync);
    assert(status.cycle_id == 42);
    assert(status.status == StatusCode::Applied);
    assert(status.apply_offset_microsecond == 20);
}

void test_rollover_and_slot_capacity()
{
    FocCycleSync rollover{SyncMode::Synchronized};
    assert(rollover.stage(command(65535), true, 10) == StageResult::Staged);
    (void) require_status(rollover);
    assert(rollover.on_sync(65535, 20) == SyncResult::Armed);
    assert(rollover.consume_armed(21).has_value());
    (void) require_status(rollover);
    assert(rollover.stage(command(0), true, 30) == StageResult::Staged);

    FocCycleSync full{SyncMode::Synchronized};
    assert(full.stage(command(7), true, 10) == StageResult::Staged);
    (void) require_status(full);
    assert(full.stage(command(8), true, 11) == StageResult::Staged);
    (void) require_status(full);
    assert(full.stage(command(9), true, 12) == StageResult::Rejected);
    const auto rejected = require_status(full);
    assert(rejected.reason == StatusReason::NoFreeSlot);
}

void test_missing_and_duplicate_sync()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.on_sync(12, 100) == SyncResult::Rejected);
    assert(require_status(sync).reason == StatusReason::NoMatchingCommand);

    assert(sync.stage(command(12), true, 110) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(12, 120) == SyncResult::Armed);
    assert(sync.consume_armed(125).has_value());
    (void) require_status(sync);
    assert(sync.on_sync(12, 130) == SyncResult::Ignored);
    assert(!sync.pop_status().has_value());
}

void test_watchdog_holds_twice_then_disables()
{
    FocCycleSync sync{SyncMode::Synchronized, 5000, 15000};
    assert(sync.stage(command(1), true, 1000) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(1, 1000) == SyncResult::Armed);
    assert(sync.poll_watchdog(5999) == WatchdogAction::None);
    assert(sync.poll_watchdog(6000) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(11000) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(16000) == WatchdogAction::Disable);
    const auto timeout = require_status(sync);
    assert(timeout.status == StatusCode::Timeout);
    assert(timeout.reason == StatusReason::Watchdog);
}

void test_immediate_mode_reports_negative_offset()
{
    FocCycleSync sync{SyncMode::Immediate};
    assert(sync.stage(command(10), true, 1000) == StageResult::Staged);
    assert(require_status(sync).status == StatusCode::Staged);
    const auto applied = sync.consume_armed(1020);
    assert(applied.has_value());
    assert(!sync.pop_status().has_value());
    assert(sync.immediate_marker(10, 1050));
    const auto status = require_status(sync);
    assert(status.status == StatusCode::Applied);
    assert(status.apply_offset_microsecond == -30);
}

void test_invalid_target_is_rejected()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(3), false, 100) == StageResult::Rejected);
    const auto status = require_status(sync);
    assert(status.status == StatusCode::Rejected);
    assert(status.reason == StatusReason::InvalidNumber);
}

}  // namespace

int main()
{
    test_staged_sync_applied_once();
    test_rollover_and_slot_capacity();
    test_missing_and_duplicate_sync();
    test_watchdog_holds_twice_then_disables();
    test_immediate_mode_reports_negative_offset();
    test_invalid_target_is_rejected();
}

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
    assert(sync.on_sync(42, SyncPhase::Run, 1000) == SyncResult::Armed);

    const auto applied = sync.consume_armed(1020);
    assert(applied.has_value());
    assert(applied->command.cycle_id == 42);
    assert(applied->apply_offset_microsecond == 20);
    assert(!sync.consume_armed(1021).has_value());
    sync.complete_apply(*applied, true);

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
    assert(rollover.on_sync(65535, SyncPhase::Run, 20) == SyncResult::Armed);
    const auto applied = rollover.consume_armed(21);
    assert(applied.has_value());
    rollover.complete_apply(*applied, true);
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
    assert(sync.on_sync(12, SyncPhase::Run, 100) == SyncResult::Rejected);
    assert(require_status(sync).reason == StatusReason::NoMatchingCommand);

    assert(sync.stage(command(12), true, 110) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(12, SyncPhase::Run, 120) == SyncResult::Armed);
    const auto applied = sync.consume_armed(125);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);
    assert(sync.on_sync(12, SyncPhase::Run, 130) == SyncResult::Ignored);
    const auto duplicate_applied = require_status(sync);
    assert(duplicate_applied.cycle_id == 12);
    assert(duplicate_applied.status == StatusCode::Applied);
    assert(duplicate_applied.reason == StatusReason::None);
    assert(duplicate_applied.apply_offset_microsecond == 5);
    assert(!sync.consume_armed(131).has_value());
    assert(sync.poll_watchdog(15119) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(15120) == WatchdogAction::Disable);
}

void test_watchdog_holds_twice_then_disables()
{
    FocCycleSync sync{SyncMode::Synchronized, 5000, 15000};
    assert(sync.stage(command(1), true, 1000) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(1, SyncPhase::Run, 1000) == SyncResult::Armed);
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
    sync.complete_apply(*applied, true);
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

void test_staged_command_does_not_start_watchdog_and_mode_change_clears_session()
{
    FocCycleSync sync{SyncMode::Synchronized, 5000, 15000};
    assert(sync.stage(command(20), true, 1000) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.poll_watchdog(16000) == WatchdogAction::None);
    assert(sync.on_sync(20, SyncPhase::Run, 16001) == SyncResult::Armed);

    sync.set_mode(SyncMode::Immediate);
    assert(sync.poll_watchdog(50000) == WatchdogAction::None);
    assert(sync.stage(command(21), true, 50001) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.consume_armed(50002).has_value());
}

void test_failed_hardware_apply_is_rejected()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(30), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(30, SyncPhase::Run, 200) == SyncResult::Armed);
    const auto applied = sync.consume_armed(210);
    assert(applied.has_value());
    sync.complete_apply(*applied, false);
    const auto status = require_status(sync);
    assert(status.status == StatusCode::Rejected);
    assert(status.reason == StatusReason::HardwareFault);
}

void test_idle_session_accepts_forward_cycle_gap()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(2), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(2, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(111);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.stage(command(20), true, 200) == StageResult::Staged);
    const auto staged = require_status(sync);
    assert(staged.status == StatusCode::Staged);
    assert(staged.reason == StatusReason::None);
}

void test_reset_session_accepts_restarted_cycle_counter()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(20), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(20, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(111);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.stage(command(19), true, 120) == StageResult::Rejected);
    assert(require_status(sync).reason == StatusReason::Stale);

    sync.reset_session();
    assert(sync.stage(command(0), true, 200) == StageResult::Staged);
    assert(require_status(sync).status == StatusCode::Staged);
    assert(sync.on_sync(0, SyncPhase::Prepare, 210) == SyncResult::Armed);
}

void test_prepare_without_matching_refreshes_only_prepared_node()
{
    FocCycleSync prepared{SyncMode::Synchronized};
    assert(prepared.stage(command(1), true, 1000) == StageResult::Staged);
    (void) require_status(prepared);
    assert(prepared.on_sync(1, SyncPhase::Prepare, 1100) == SyncResult::Armed);
    const auto applied = prepared.consume_armed(1110);
    assert(applied.has_value());
    prepared.complete_apply(*applied, true);
    (void) require_status(prepared);

    assert(prepared.on_sync(2, SyncPhase::Prepare, 200000) == SyncResult::Ignored);
    assert(!prepared.pop_status().has_value());
    assert(prepared.poll_watchdog(449999) == WatchdogAction::Hold);
    assert(prepared.poll_watchdog(450000) == WatchdogAction::Disable);

    FocCycleSync idle{SyncMode::Synchronized};
    assert(idle.on_sync(5, SyncPhase::Prepare, 100) == SyncResult::Ignored);
    assert(!idle.pop_status().has_value());
    assert(idle.poll_watchdog(1000000) == WatchdogAction::None);
}

void test_prepare_timeout_disables_at_250_ms()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(3), true, 1000) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(3, SyncPhase::Prepare, 1010) == SyncResult::Armed);
    const auto applied = sync.consume_armed(1020);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.poll_watchdog(251009) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(251010) == WatchdogAction::Disable);
}

void test_matching_run_arms_15_ms_watchdog()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(4), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(4, SyncPhase::Prepare, 110) == SyncResult::Armed);
    auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.stage(command(5), true, 1000) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(5, SyncPhase::Run, 1010) == SyncResult::Armed);
    applied = sync.consume_armed(1020);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.poll_watchdog(16009) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(16010) == WatchdogAction::Disable);
}

void test_prepare_cannot_downgrade_run()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(6), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(6, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    assert(sync.on_sync(7, SyncPhase::Prepare, 10000) == SyncResult::Rejected);
    assert(require_status(sync).reason == StatusReason::OutOfRange);
    assert(sync.poll_watchdog(15110) == WatchdogAction::Disable);
}

void test_unknown_phase_is_rejected()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.on_sync(8, static_cast<SyncPhase>(2U), 100) == SyncResult::Rejected);
    assert(require_status(sync).reason == StatusReason::OutOfRange);
    assert(sync.poll_watchdog(1000000) == WatchdogAction::None);

    FocCycleSync immediate{SyncMode::Immediate};
    assert(immediate.on_sync(9, static_cast<SyncPhase>(255U), 200) == SyncResult::Rejected);
    assert(require_status(immediate).reason == StatusReason::OutOfRange);
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
    test_staged_command_does_not_start_watchdog_and_mode_change_clears_session();
    test_failed_hardware_apply_is_rejected();
    test_idle_session_accepts_forward_cycle_gap();
    test_reset_session_accepts_restarted_cycle_counter();
    test_prepare_without_matching_refreshes_only_prepared_node();
    test_prepare_timeout_disables_at_250_ms();
    test_matching_run_arms_15_ms_watchdog();
    test_prepare_cannot_downgrade_run();
    test_unknown_phase_is_rejected();
}

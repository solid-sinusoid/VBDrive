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
    // On a loaded CAN bus the RUN marker can leave FIFO1 before its matching
    // command. It must not be rejected: the marker is deferred until exactly
    // that command arrives, and cannot apply any other command.
    assert(sync.on_sync(12, SyncPhase::Run, 100) == SyncResult::Ignored);
    assert(!sync.pop_status().has_value());

    assert(sync.stage(command(12), true, 110) == StageResult::Staged);
    const auto staged = require_status(sync);
    assert(staged.status == StatusCode::Staged);
    assert(staged.cycle_id == 12);
    // The deferred RUN has armed only this matching cycle, without waiting
    // for a second marker.
    const auto applied = sync.consume_armed(125);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);
    assert(sync.on_sync(12, SyncPhase::Run, 130) == SyncResult::Ignored);
    // Повторный RUN запрашивает повтор APPLIED через отдельный mailbox, не
    // занимая FIFO, предназначенный для STAGED/REJECTED.
    const auto duplicate_applied = require_status(sync);
    assert(duplicate_applied.cycle_id == 12);
    assert(duplicate_applied.status == StatusCode::Applied);
    assert(duplicate_applied.reason == StatusReason::None);
    assert(duplicate_applied.apply_offset_microsecond == 25);
    assert(!sync.consume_armed(131).has_value());
    // A repeated RUN marker is an idempotent acknowledgement probe.  It must
    // also keep the RUN watchdog alive while the main loop publishes APPLIED.
    assert(sync.poll_watchdog(50129) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(50130) == WatchdogAction::Disable);
}

void test_deferred_run_never_arms_different_or_superseded_cycle()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.on_sync(80, SyncPhase::Run, 100) == SyncResult::Ignored);

    // A later command proves cycle 80 was superseded; the deferred RUN must
    // not arm it or any other cycle.
    assert(sync.stage(command(81), true, 110) == StageResult::Staged);
    (void) require_status(sync);
    assert(!sync.consume_armed(111).has_value());

    assert(sync.stage(command(80), true, 120) == StageResult::Staged);
    (void) require_status(sync);
    assert(!sync.consume_armed(121).has_value());
}

void test_repeated_applied_run_does_not_starve_next_staged()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(71), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(71, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);

    // Хост повторяет RUN, пока ждёт подтверждение. Очередь на 7 записей не
    // должна быть заполнена дубликатами APPLIED и вытеснить новый STAGED.
    for (std::uint64_t timestamp = 130; timestamp < 230; timestamp += 5) {
        assert(sync.on_sync(71, SyncPhase::Run, timestamp) == SyncResult::Ignored);
    }
    assert(sync.stage(command(72), true, 230) == StageResult::Staged);

    const auto staged = require_status(sync);
    assert(staged.cycle_id == 72);
    assert(staged.status == StatusCode::Staged);
    const auto applied_status = require_status(sync);
    assert(applied_status.cycle_id == 71);
    assert(applied_status.status == StatusCode::Applied);
    assert(!sync.pop_status().has_value());
}

void test_duplicate_command_is_idempotent_before_sync()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(12), true, 100) == StageResult::Staged);
    (void) require_status(sync);

    assert(sync.stage(command(12), true, 101) == StageResult::Staged);
    const auto duplicate = require_status(sync);
    assert(duplicate.cycle_id == 12);
    assert(duplicate.status == StatusCode::Staged);
    assert(duplicate.reason == StatusReason::None);

    assert(sync.on_sync(12, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);
    assert(!sync.consume_armed(121).has_value());

    assert(sync.stage(command(13), true, 130) == StageResult::Staged);
    assert(require_status(sync).cycle_id == 13);
}

void test_duplicate_run_sync_is_idempotent_before_apply()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(14), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(14, SyncPhase::Run, 110) == SyncResult::Armed);
    assert(sync.on_sync(14, SyncPhase::Run, 111) == SyncResult::Ignored);

    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    const auto status = require_status(sync);
    assert(status.cycle_id == 14);
    assert(status.status == StatusCode::Applied);
    assert(!sync.consume_armed(121).has_value());
}

void test_duplicate_run_sync_keeps_watchdog_alive_while_armed()
{
    FocCycleSync sync{SyncMode::Synchronized, 5000, 15000};
    assert(sync.stage(command(145), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(145, SyncPhase::Run, 110) == SyncResult::Armed);

    // На реальном приводе FOC ISR может задержать consume_armed(). Повторный
    // RUN должен продлить watchdog и не сбросить ещё ARMED-команду.
    assert(sync.on_sync(145, SyncPhase::Run, 10000) == SyncResult::Ignored);
    assert(sync.poll_watchdog(20000) == WatchdogAction::Hold);
    assert(sync.consume_armed(20001).has_value());
}

void test_duplicate_run_sync_is_idempotent_while_apply_is_in_progress()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(15), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(15, SyncPhase::Run, 110) == SyncResult::Armed);

    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    // The FOC ISR has consumed the slot but has not published APPLIED yet.
    // A repeated marker must be harmless during precisely this interval.
    assert(sync.on_sync(15, SyncPhase::Run, 121) == SyncResult::Ignored);
    assert(!sync.pop_status().has_value());

    sync.complete_apply(*applied, true);
    const auto status = require_status(sync);
    assert(status.cycle_id == 15);
    assert(status.status == StatusCode::Applied);
}

void test_run_progress_survives_session_reset()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.run_progress() == 0U);

    assert(sync.stage(command(16), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.on_sync(16, SyncPhase::Run, 110) == SyncResult::Armed);
    const auto applied = sync.consume_armed(120);
    assert(applied.has_value());
    sync.complete_apply(*applied, true);
    (void) require_status(sync);

    // Counters are intentionally retained after a watchdog/deactivation reset
    // so a host can inspect the last failed synchronized bring-up.
    assert(sync.run_progress() == 0x00010101U);
    assert(sync.run_status_progress() == 0x0100U);
    assert(sync.on_sync(16, SyncPhase::Run, 121) == SyncResult::Ignored);
    const auto duplicate = require_status(sync);
    assert(duplicate.status == StatusCode::Applied);
    assert(sync.run_status_progress() == 0x0201U);
    sync.reset_session();
    assert(sync.run_progress() == 0x00010101U);
    assert(sync.run_status_progress() == 0x0201U);
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

void test_command_progress_keeps_last_received_cycle_after_rejection()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(101), true, 100) == StageResult::Staged);
    (void) require_status(sync);
    assert(sync.staged_status_progress() == 0x00650001U);
    assert(sync.stage(command(102), false, 101) == StageResult::Rejected);
    assert(sync.stage(command(0), true, 102) == StageResult::Staged);
    assert(sync.command_progress() == 0x00660203U);
}

void test_direct_staged_status_progress_is_retained()
{
    FocCycleSync sync{SyncMode::Synchronized};
    sync.note_staged_status_published(123U);
    assert(sync.staged_status_progress() == 0x007B0001U);
    sync.note_staged_status_published(0U);
    assert(sync.staged_status_progress() == 0x007B0001U);
}

void test_duplicate_staged_commands_coalesce_retry_status()
{
    FocCycleSync sync{SyncMode::Synchronized};
    assert(sync.stage(command(201), true, 100) == StageResult::Staged);
    for (std::uint64_t time = 101; time < 120; ++time) {
        assert(sync.stage(command(201), true, time) == StageResult::Staged);
    }
    assert(require_status(sync).cycle_id == 201U);

    assert(sync.stage(command(202), true, 120) == StageResult::Staged);
    assert(require_status(sync).cycle_id == 202U);
    assert(require_status(sync).cycle_id == 201U);
    assert(!sync.pop_status().has_value());
}

void test_main_status_queue_overflow_retains_drop_diagnostic()
{
    FocCycleSync sync{SyncMode::Synchronized};

    // The ring has seven usable entries.  The eighth status must not silently
    // erase the evidence needed to diagnose a delayed acknowledgement on a
    // physical drive.
    for (std::uint16_t cycle_id = 1U; cycle_id <= 8U; ++cycle_id) {
        assert(sync.stage(command(cycle_id), false, cycle_id) == StageResult::Rejected);
    }

    assert(sync.status_queue_progress() == 0x00080001U);
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

void test_matching_run_arms_default_50_ms_watchdog()
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

    // При цикле 200 Гц окно в три периода (15 мс) недостаточно для
    // кратковременного джиттера шины и главного цикла во время group RUN.
    // Десять периодов удерживают привод при таком джиттере, но сохраняют
    // fail-closed через 50 мс при настоящей потере SYNC.
    assert(sync.poll_watchdog(51009) == WatchdogAction::Hold);
    assert(sync.poll_watchdog(51010) == WatchdogAction::Disable);
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
    assert(sync.poll_watchdog(50110) == WatchdogAction::Disable);
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
    test_deferred_run_never_arms_different_or_superseded_cycle();
    test_repeated_applied_run_does_not_starve_next_staged();
    test_duplicate_command_is_idempotent_before_sync();
    test_duplicate_run_sync_is_idempotent_before_apply();
    test_duplicate_run_sync_keeps_watchdog_alive_while_armed();
    test_duplicate_run_sync_is_idempotent_while_apply_is_in_progress();
    test_run_progress_survives_session_reset();
    test_watchdog_holds_twice_then_disables();
    test_immediate_mode_reports_negative_offset();
    test_invalid_target_is_rejected();
    test_command_progress_keeps_last_received_cycle_after_rejection();
    test_direct_staged_status_progress_is_retained();
    test_duplicate_staged_commands_coalesce_retry_status();
    test_main_status_queue_overflow_retains_drop_diagnostic();
    test_staged_command_does_not_start_watchdog_and_mode_change_clears_session();
    test_failed_hardware_apply_is_rejected();
    test_idle_session_accepts_forward_cycle_gap();
    test_reset_session_accepts_restarted_cycle_counter();
    test_prepare_without_matching_refreshes_only_prepared_node();
    test_prepare_timeout_disables_at_250_ms();
    test_matching_run_arms_default_50_ms_watchdog();
    test_prepare_cannot_downgrade_run();
    test_unknown_phase_is_rejected();
}

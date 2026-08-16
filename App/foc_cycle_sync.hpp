#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <optional>

enum class SyncMode : std::uint8_t { Immediate = 0, Synchronized = 1 };
enum class SyncPhase : std::uint8_t { Prepare = 0, Run = 1 };
enum class StatusCode : std::uint8_t { Staged = 0, Applied = 1, Rejected = 2, Timeout = 3 };
enum class StatusReason : std::uint8_t {
    None = 0,
    InvalidNumber = 1,
    OutOfRange = 2,
    Stale = 3,
    TooFarAhead = 4,
    NoFreeSlot = 5,
    NoMatchingCommand = 6,
    HardwareFault = 7,
    Watchdog = 8,
};
enum class StageResult : std::uint8_t { Staged, Rejected };
enum class SyncResult : std::uint8_t { Armed, Ignored, Rejected };
enum class WatchdogAction : std::uint8_t { None, Hold, Disable };

struct FocCycleTarget {
    float torque{};
    float angle{};
    float velocity{};
    float angle_kp{};
    float velocity_kp{};
};

struct CycleCommand {
    std::uint16_t cycle_id{};
    FocCycleTarget target{};
    float current_kp{};
    float current_ki{};
};

struct AppliedCycle {
    CycleCommand command{};
    SyncPhase phase{SyncPhase::Run};
    std::int32_t apply_offset_microsecond{};
    std::uint64_t apply_timestamp_us{};
};

struct CommandStatus {
    std::uint16_t cycle_id{};
    StatusCode status{StatusCode::Staged};
    StatusReason reason{StatusReason::None};
    std::int32_t apply_offset_microsecond{};
};

constexpr bool cycle_after(const std::uint16_t lhs, const std::uint16_t rhs)
{
    return static_cast<std::int16_t>(lhs - rhs) > 0;
}

class FocCycleSync {
public:
    explicit FocCycleSync(
        SyncMode mode,
        std::uint32_t control_period_us = 5000,
        std::uint32_t watchdog_us = 15000);

    void reset_session();
    void set_mode(SyncMode mode);
    SyncMode mode() const { return mode_; }
    StageResult stage(const CycleCommand& command, bool target_valid, std::uint64_t rx_us);
    SyncResult on_sync(std::uint16_t cycle_id, SyncPhase phase, std::uint64_t rx_us);
    std::optional<AppliedCycle> consume_armed(std::uint64_t apply_us);
    void complete_apply(const AppliedCycle& applied, bool accepted);
    bool immediate_marker(std::uint16_t cycle_id, std::uint64_t marker_us);
    WatchdogAction poll_watchdog(std::uint64_t now_us);
    std::optional<CommandStatus> pop_status();
    // Four 8-bit counters: RUN armed, consumed by the FOC ISR, completed,
    // and rejected. They deliberately survive reset_session() so they retain
    // the cause of a failed host activation after the motor has been disabled.
    std::uint32_t run_progress() const;
    // Low byte: repeated RUN markers accepted after apply. High byte: RUN
    // APPLIED statuses removed from the firmware mailbox for CAN publishing.
    std::uint16_t run_status_progress() const;

private:
    enum class SlotState : std::uint8_t { Empty, Writing, Staged, Armed };

    struct Slot {
        std::atomic<SlotState> state{SlotState::Empty};
        CycleCommand command{};
        SyncPhase phase{SyncPhase::Run};
        std::uint64_t marker_us{};
    };

    struct StatusMailbox {
        CommandStatus status{};
        std::atomic<bool> valid{false};
    };

    struct ImmediateApplyMailbox {
        std::uint16_t cycle_id{};
        std::uint64_t apply_us{};
        std::atomic<bool> valid{false};
    };

    static constexpr std::uint8_t no_slot = std::numeric_limits<std::uint8_t>::max();
    static constexpr std::size_t status_capacity = 8;
    static constexpr std::uint32_t prepare_watchdog_us = 250000U;

    enum class SessionPhase : std::uint8_t { Idle, Prepare, Run };

    void push_main_status(CommandStatus status);
    void publish_applied_from_isr(CommandStatus status);
    Slot* find_slot(std::uint16_t cycle_id, SlotState required_state);
    const Slot* find_slot(std::uint16_t cycle_id, SlotState required_state) const;
    static std::int32_t saturated_offset(std::uint64_t apply_us, std::uint64_t marker_us);

    SyncMode mode_;
    const std::uint32_t control_period_us_;
    const std::uint32_t watchdog_us_;
    std::array<Slot, 2> slots_{};
    std::atomic<std::uint8_t> armed_slot_{no_slot};
    std::atomic<std::uint16_t> last_applied_cycle_{};
    std::atomic<std::uint8_t> last_applied_phase_{};
    std::atomic<std::int32_t> last_apply_offset_microsecond_{};
    std::atomic<bool> has_last_applied_{false};
    std::atomic<std::uint16_t> applying_cycle_{};
    std::atomic<std::uint8_t> applying_phase_{};
    std::atomic<bool> has_applying_cycle_{false};
    std::atomic<std::uint8_t> run_armed_count_{};
    std::atomic<std::uint8_t> run_consumed_count_{};
    std::atomic<std::uint8_t> run_completed_count_{};
    std::atomic<std::uint8_t> run_rejected_count_{};
    std::atomic<std::uint8_t> run_repeat_sync_count_{};
    std::atomic<std::uint8_t> run_applied_status_pop_count_{};
    std::uint64_t last_sync_us_{};
    bool has_last_sync_{false};
    bool watchdog_reported_{false};
    SessionPhase session_phase_{SessionPhase::Idle};
    std::array<CommandStatus, status_capacity> main_statuses_{};
    std::uint8_t main_status_head_{};
    std::uint8_t main_status_tail_{};
    StatusMailbox applied_mailbox_{};
    ImmediateApplyMailbox immediate_apply_{};
};

static_assert(std::atomic<std::uint8_t>::is_always_lock_free);
static_assert(std::atomic<std::uint16_t>::is_always_lock_free);
static_assert(std::atomic<std::int32_t>::is_always_lock_free);

#include "../App/disable_diagnostics.hpp"

#include <cassert>

int main()
{
    MotorDisableDiagnostics diagnostics;

    assert(diagnostics.reason() == MotorDisableReason::None);
    assert(diagnostics.count() == 0U);

    diagnostics.record(MotorDisableReason::RegisterStop);
    assert(diagnostics.reason() == MotorDisableReason::RegisterStop);
    assert(diagnostics.count() == 1U);

    diagnostics.record(MotorDisableReason::SyncWatchdog);
    assert(diagnostics.reason() == MotorDisableReason::SyncWatchdog);
    assert(diagnostics.count() == 2U);
    assert(diagnostics.packed_state() == 0x00000202U);

    const FdcanDiagnosticSnapshot can{
        .tx_error_count = 0x12U,
        .rx_error_count = 0x34U,
        .error_logging_count = 0x56U,
        .last_error_code = 5U,
        .data_last_error_code = 6U,
        .bus_off = true,
        .error_passive = false,
        .warning = true,
        .protocol_exception = true,
        .rx_error_passive = true,
    };
    assert(pack_fdcan_diagnostics(can) == 0x75563412U);
}

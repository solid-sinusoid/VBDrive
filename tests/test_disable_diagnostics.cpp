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
}

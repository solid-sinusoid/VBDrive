#!/usr/bin/env python3
"""Compile the real VBDrive::start body with fault injection at each enable boundary."""
import subprocess
import sys
import tempfile
from pathlib import Path

source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'Drivers/libvoltbro/voltbro/motors/bldc/vbdrive/vbdrive.hpp'
text = source.read_text()
start = text.index('HAL_StatusTypeDef start_guarded(') if 'HAL_StatusTypeDef start_guarded(' in text else text.index('HAL_StatusTypeDef start() override {')
opening = text.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
body = text[opening:end]
def function(source, signature):
    begin = source.index(signature)
    opening = source.index('{', begin)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]
state = Path(__file__).resolve().parents[1] / 'App/state_manager/state_manager.cpp'
manager_methods = '\n'.join(function(state.read_text(), sig) for sig in (
    'ParameterWriteResult DriveStateController::set_motor_enabled(',
    'bool DriveStateController::service_temperature()'))
test = r'''
#include <cassert>
#include <cstdint>
#include "foc_cycle_sync.hpp"
#define HAL_OK 0
#define HAL_ERROR 1
using HAL_StatusTypeDef = int;
enum class VBDriveStatus { NOMINAL, COOLING_DOWN };
enum class ParameterWriteResult { OK, INVALID, UNAVAILABLE };
enum class MotorDisableReason { RegisterStop, SyncWatchdog };
struct { unsigned CNT=0; } timer;
auto* TIM2=&timer;
bool moe = false, failed = false, armed = false; int inject = 0;
void irq_stop();
FocCycleSync watchdog{SyncMode::Immediate};
void arm() { armed = true; watchdog.start_immediate_watchdog(timer.CNT); }
struct { void start_immediate_watchdog(unsigned){arm();} } foc_cycle_sync;
void reset_foc_cycle_session(){armed=false;watchdog.reset_session();}
struct { unsigned value=0; void record(MotorDisableReason){++value;} unsigned count(){return value;} } motor_disable_diagnostics;
#define VBDRIVE_PROFILE_HANDLER(x)
struct Sensor { bool healthy() const { return !failed; } };
struct Gate { int wake() { if (inject == 1) { failed = true; moe = false; } if(inject==3)irq_stop(); return HAL_OK; }
              int clear_faults() { if (inject == 2) { failed = true; moe = false; }
                  if(inject==4){timer.CNT+=500;assert(watchdog.immediate_watchdog_expired(timer.CNT));irq_stop();} return HAL_OK; } };
#define CRITICAL_SECTION(code) { code }
#define __HAL_TIM_MOE_ENABLE(h) (moe = true)
#define __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(h) (moe = false)
unsigned HAL_GetTick() { return 0; }
struct Drive {
    VBDriveStatus thermal_status = VBDriveStatus::NOMINAL;
    Sensor encoder, inductive_sensor;
    Gate gate_driver;
    unsigned bootstrap_charge_deadline_ms = 0;
    static constexpr unsigned bootstrap_charge_time_ms = 5;
    bool _is_on = false; int* htim = nullptr;
    uint32_t stop_generation = 0;
    void update_thermal_protection() {}
    int stop() { ++stop_generation; moe = false; _is_on = false; return HAL_OK; }
    void force_bootstrap_charge() {}
    void quit_stall() {}
    int start_guarded(uint32_t expected_generation, void (*before_enable)()) BODY
    int start() {return start_guarded(stop_generation, arm);}
    uint32_t get_stop_generation() const {return stop_generation;}
    bool is_on() {bool result=_is_on; if(inject==5){inject=0;irq_stop();} return result;}
    VBDriveStatus get_status() const {return thermal_status;}
    void reset_servo_input() {}
    int set_state(bool enabled) {return enabled ? start() : stop();}
};
Drive* active;
void irq_stop(){active->stop();reset_foc_cycle_session();}
Drive* get_motor(){return active;}
struct DriveStateController {
    bool restore_motor_enable=false;
    bool is_app_running(){return true;}
    ParameterWriteResult set_motor_enabled(bool enabled, MotorDisableReason reason=MotorDisableReason::RegisterStop);
    bool service_temperature();
};
MANAGER_METHODS
int main() {
    { Drive d; active=&d; inject=4; assert(d.start()==HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; active=&d; inject=3; assert(d.start()==HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; active=&d; inject=0; auto request=d.stop_generation; d.stop();
      assert(d.start_guarded(request,arm)==HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; failed = true; assert(d.start() == HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; failed = false; inject = 1; assert(d.start() == HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; failed = false; inject = 2; assert(d.start() == HAL_ERROR); assert(!moe && !d._is_on); }
    { Drive d; failed = false; inject = 0; assert(d.start() == HAL_OK); assert(moe && d._is_on); }
    { Drive d; active=&d; DriveStateController manager; armed=false; inject=0;
      assert(manager.set_motor_enabled(true)==ParameterWriteResult::OK); assert(moe && d._is_on && armed);
      inject=5; manager.set_motor_enabled(true); assert(!moe && !d._is_on && !armed); }
    { Drive d; active=&d; DriveStateController manager; manager.restore_motor_enable=true; inject=4;
      assert(!manager.service_temperature()); assert(!moe && !d._is_on && !armed); }
}
'''.replace('BODY', body).replace('MANAGER_METHODS', manager_methods)
with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
    cpp = Path(directory) / 'test.cpp'
    exe = Path(directory) / 'test'
    cpp.write_text(test)
    root = Path(__file__).resolve().parents[1]
    subprocess.run(['g++', '-std=c++20', '-I'+str(root/'App'), str(cpp), str(root/'App/foc_cycle_sync.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('VBDrive::start actual body: latched fault and two enable races PASS')

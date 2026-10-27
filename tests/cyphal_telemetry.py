#!/usr/bin/env python3
"""Execute production telemetry scheduling with a clock and CAN sink; no hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / 'App/communications/cyphal/interface.hpp').read_text()
start = text.index('void in_loop_reporting(')
end = text.index('\nclass FOCCommandSub', start)
program = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>
#include <voltbro/foc/State_1_0.h>
#include <voltbro/foc/state_simple_1_0.h>
using millis=uint32_t;
using CanardTransferID=uint8_t;
constexpr unsigned FOC_STATE_PORT=3811;
constexpr millis FOC_STATE_PERIOD_MS=5;
#define VBDRIVE_PROFILE_COUNT(counter)
uint64_t now_us=0;
uint64_t system_time(){return now_us;}
struct Motor {
    bool on=true;
    float get_angle(){return 1.25f;}
    float get_velocity(){return -.5f;}
    float get_torque(){return 3;}
    float get_working_current(){return 1;}
    bool is_on(){return on;}
} motor;
auto get_motor(){return &motor;}
struct Inverter {
    float stator=25,mcu=30;
    void update_temperature(){}
    float get_stator_temperature(){return stator;}
    float get_mcu_temperature(){return mcu;}
    float get_busV(){return 24;}
} motor_inverter;
struct Sink {
    std::vector<uint64_t> times;
    voltbro_foc_state_simple_1_0 last{};
    void send_msg(voltbro_foc_State_1_0*,unsigned port,CanardTransferID*,uint64_t=0){
        assert(port==3811);times.push_back(now_us);
    }
    void send_msg(voltbro_foc_state_simple_1_0* msg,unsigned port,CanardTransferID*,uint64_t=0){
        assert(port==3811);times.push_back(now_us);last=*msg;
    }
} sink;
auto get_interface(){return &sink;}
''' + text[start:end] + r'''
void report(uint32_t ms){now_us=uint64_t(ms)*1000;in_loop_reporting(ms);}
int main(){
    for(uint32_t ms=0;ms<=20;++ms) report(ms);
    assert((sink.times==std::vector<uint64_t>{5000,10000,15000,20000}));
    report(100);report(100);report(101);report(104);
    assert(sink.times.size()==5 && sink.times.back()==100000);
    report(105);assert(sink.times.size()==6);
    report(UINT32_MAX-2);const auto before=sink.times.size();
    report(0);report(1);assert(sink.times.size()==before);
    report(2);assert(sink.times.size()==before+1);
    assert(sink.last.angle.radian==1.25f && sink.last.velocity.radian_per_second==-.5f);
    assert(sink.last._torque.newton_meter==3 && sink.last.current.ampere==1);
    assert(sink.last.bus_voltage.volt==24);
    assert(std::fabs(sink.last.mcu_temp.kelvin-303.15f)<.001f);
    motor.on=false;report(7);
    assert(sink.last._torque.newton_meter==0 && sink.last.current.ampere==0);
    // Rising temperature and invalid sensor readings cannot be hidden by telemetry smoothing.
    motor_inverter.stator=95;report(12);
    assert(std::fabs(sink.last.stator_temp.kelvin-368.15f)<.001f);
    motor_inverter.stator=25;report(17);
    assert(sink.last.stator_temp.kelvin>368.14f);
    motor_inverter.stator=NAN;report(22);assert(std::isnan(sink.last.stator_temp.kelvin));
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-telemetry-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(root/'build/Release/cyphal_types/c'),
                    str(path/'test.cpp'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
print('PASS: 200 Hz fresh telemetry, timer wrap, OFF current/torque and temperature reporting')

#!/usr/bin/env python3
"""Golden wire vectors for the actual generated FOC synchronization types."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <voltbro/foc/command_2_0.h>
#include <voltbro/foc/sync_2_0.h>
#include <voltbro/foc/command_status_1_0.h>

int main() {
    voltbro_foc_command_2_0 command{};
    command.cycle_id=0x1234;
    command._torque.newton_meter=1;
    command.angle.radian=2;
    command.velocity.radian_per_second=-1;
    command.angle_kp.value=.5f;
    command.velocity_kp.value=.25f;
    command.I_kp.value=3;
    command.I_ki.value=4;
    std::array<uint8_t,30> wire{};
    size_t size=wire.size();
    assert(voltbro_foc_command_2_0_serialize_(&command,wire.data(),&size)==0);
    const std::array<uint8_t,30> expected{
        0x34,0x12,0,0,0x80,0x3f,0,0,0,0x40,0,0,0x80,0xbf,
        0,0,0,0x3f,0,0,0x80,0x3e,0,0,0x40,0x40,0,0,0x80,0x40};
    assert(size==30 && wire==expected);
    voltbro_foc_command_2_0 decoded{};
    assert(voltbro_foc_command_2_0_deserialize_(&decoded,wire.data(),&size)==0);
    assert(decoded.cycle_id==0x1234 && decoded._torque.newton_meter==1 &&
           decoded.angle.radian==2 && decoded.velocity.radian_per_second==-1 &&
           decoded.angle_kp.value==.5f && decoded.velocity_kp.value==.25f &&
           decoded.I_kp.value==3 && decoded.I_ki.value==4);

    voltbro_foc_sync_2_0 sync{};
    sync.cycle_id=0x1234;
    sync.phase=voltbro_foc_sync_2_0_PHASE_RUN;
    std::array<uint8_t,3> marker{};
    size=marker.size();
    assert(voltbro_foc_sync_2_0_serialize_(&sync,marker.data(),&size)==0);
    assert(size==3 && (marker==std::array<uint8_t,3>{0x34,0x12,1}));
    sync.phase=voltbro_foc_sync_2_0_PHASE_PREPARE;
    assert(voltbro_foc_sync_2_0_serialize_(&sync,marker.data(),&size)==0);
    assert((marker==std::array<uint8_t,3>{0x34,0x12,0}));

    voltbro_foc_command_status_1_0 status{};
    status.cycle_id=0x1234;
    status.status=voltbro_foc_command_status_1_0_STATUS_APPLIED;
    status.reason=voltbro_foc_command_status_1_0_REASON_WATCHDOG;
    status.apply_offset_microsecond=-25;
    std::array<uint8_t,8> ack{};
    size=ack.size();
    assert(voltbro_foc_command_status_1_0_serialize_(&status,ack.data(),&size)==0);
    assert(size==8 && (ack==std::array<uint8_t,8>{0x34,0x12,1,8,0xe7,0xff,0xff,0xff}));
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-sync-wire-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(source)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(root / 'build/Release/cyphal_types/c'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('PASS: generated command2, phased sync2 and status1 match independent wire vectors')

#!/usr/bin/env python3
"""Optional offline integration: actual client codec into firmware CAN/IRQ adapters."""
from pathlib import Path
import os
import hashlib
import runpy
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
client_env=os.environ.get('VBDRIVE_CLIENT_REPO')
if not client_env:
    print('SKIP: set VBDRIVE_CLIENT_REPO to the client checkout; no hardware was used')
    raise SystemExit(0)
client=Path(client_env)/'vbdrive_cyphal_hardware'
codec_path=client/'src/cyphal_codec.cpp'
codec_sha=hashlib.sha256(codec_path.read_bytes()).hexdigest()
test=runpy.run_path(str(root/'tests/synchronized_control.py'))
program=test['program']
program=program.replace('#include <cassert>', '#include <cassert>\n#include "vbdrive_cyphal_hardware/cyphal_codec.hpp"\n#include <voltbro/foc/state_simple_1_0.h>')
check=r'''
    // The actual client encoder drives real libcanard reassembly and the
    // firmware subscription/IRQ. The actual client decoder reads the ACK.
    {
        namespace host=vbdrive_cyphal_hardware::cyphal;
        reset_foc_cycle_session();device=Motor{};sink.statuses.clear();
        auto alloc=+[](CanardInstance*,size_t n){return std::malloc(n);};
        auto free=+[](CanardInstance*,void* p){std::free(p);};
        auto rx=canardInit(alloc,free);rx.node_id=1;
        CanardRxSubscription command_rx{},sync_rx{};
        assert(canardRxSubscribe(&rx,CanardTransferKindMessage,2301,30,0,&command_rx)>=0);
        assert(canardRxSubscribe(&rx,CanardTransferKindMessage,3809,3,0,&sync_rx)>=0);
        host::FocCommand actual{};actual.cycle_id=15;actual.torque_nm=1.25f;
        actual.angle_rad=.5f;actual.current_kp=3;actual.current_ki=400;
        auto bytes=host::encode_foc_command_payload(actual,0);
        assert(bytes.size()==32);
        CanardFrame frame{host::make_message_can_id(0,2301,42),bytes.size(),bytes.data()};
        CanardRxTransfer received{};
        assert(canardRxAccept(&rx,now_us,&frame,0,&received,nullptr)==1);
        command.accept(&received);free(&rx,received.payload);
        assert(device.targets==0);
        auto sync_bytes=host::encode_foc_sync_payload({15,host::SyncPhase::Run},0);
        frame={host::make_message_can_id(0,3809,42),sync_bytes.size(),sync_bytes.data()};
        assert(canardRxAccept(&rx,now_us,&frame,0,&received,nullptr)==1);
        sync.accept(&received);free(&rx,received.payload);
        now_us+=25;main_callback();
        assert(device.targets==1 && device.target.torque==1.25f && device.target.angle==.5f);
        std::optional<CommandStatus> ack;
        while(auto status=foc_cycle_sync.pop_status())if(status->status==StatusCode::Applied)ack=status;
        assert(ack);
        voltbro_foc_command_status_1_0 firmware_ack{};
        firmware_ack.cycle_id=ack->cycle_id;firmware_ack.status=static_cast<uint8_t>(ack->status);
        firmware_ack.reason=static_cast<uint8_t>(ack->reason);
        firmware_ack.apply_offset_microsecond=ack->apply_offset_microsecond;
        uint8_t wire[13]{};size_t size=8;
        assert(voltbro_foc_command_status_1_0_serialize_(&firmware_ack,wire,&size)==0 && size==8);
        wire[12]=host::make_tail_byte(true,true,true,0);
        host::CommandStatus decoded{};
        assert(host::decode_command_status_payload({wire,13},decoded));
        assert(decoded.cycle_id==15 && decoded.status==host::CommandStatusCode::Applied && decoded.apply_offset_microsecond==25);
        voltbro_foc_state_simple_1_0 state{};
        state.timestamp.microsecond=123456;state.angle.radian=.5f;
        state.velocity.radian_per_second=-1;state._torque.newton_meter=1.25f;
        state.current.ampere=2;state.bus_voltage.volt=24;
        state.mcu_temp.kelvin=303.15f;state.stator_temp.kelvin=310.15f;
        uint8_t state_wire[49]{};size=sizeof(state_wire)-1;
        assert(voltbro_foc_state_simple_1_0_serialize_(&state,state_wire,&size)==0);
        state_wire[size]=host::make_tail_byte(true,true,true,0);
        host::FocStateSimple host_state{};
        assert(host::decode_state_simple_payload({state_wire,size+1},host_state));
        assert(host_state.timestamp_us==123456 && host_state.angle_rad==.5f &&
               host_state.velocity_rad_per_s==-1 && host_state.torque_nm==1.25f &&
               host_state.current_a==2 && host_state.bus_voltage_v==24 &&
               std::fabs(host_state.stator_temp_k-310.15f)<.001f);
        canardRxUnsubscribe(&rx,CanardTransferKindMessage,2301);
        canardRxUnsubscribe(&rx,CanardTransferKindMessage,3809);
    }
'''
marker='    // Characterize the overlap:'
assert marker in program
program=program.replace(marker,check+'\n'+marker)
with tempfile.TemporaryDirectory(prefix='vbdrive-client-firmware-') as d:
    p=Path(d);(p/'test.cpp').write_text(program)
    lib=root/'Drivers/libcxxcanard/libs/libcanard'
    subprocess.run(['cc','-std=c11','-I'+str(lib),'-c',str(lib/'canard.c'),'-o',str(p/'canard.o')],check=True)
    subprocess.run(['c++','-std=c++20','-O2','-I'+str(client/'include'),
                    '-I'+str(client/'third_party/libcxxcanard'),'-c',str(codec_path),
                    '-o',str(p/'codec.o')],check=True)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(root/'App'),
                    '-I'+str(lib.parent),'-I'+str(root/'build/Release/cyphal_types/c'),
                    '-I'+str(client/'include'),str(p/'test.cpp'),str(root/'App/foc_cycle_sync.cpp'),
                    str(p/'canard.o'),str(p/'codec.o'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print(f'PASS: actual client codec → libcanard → production sync IRQ → client ACK decoder; codec SHA256={codec_sha}; no hardware')

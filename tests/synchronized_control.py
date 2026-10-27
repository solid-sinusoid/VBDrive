#!/usr/bin/env python3
"""Exercise production subscriptions, cycle machine and IRQ adapter with hardware doubles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
interface = (root/'App/communications/cyphal/interface.hpp').read_text()
app = (root/'App/app.cpp').read_text()

def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

prefix = r'''
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>
#include <libcanard/canard.h>
#include <voltbro/foc/command_2_0.h>
#include <voltbro/foc/sync_2_0.h>
#include <voltbro/foc/command_status_1_0.h>
#include "foc_cycle_sync.hpp"
#include "disable_diagnostics.hpp"
#include "fdcan_tx_policy.hpp"
#define VBDRIVE_PROFILE_RESULT(kind, accepted)
#define VBDRIVE_PROFILE_HANDLER(stats)
#define CRITICAL_SECTION(code) {code}
constexpr int HAL_OK=0;
constexpr unsigned DEFAULT_TIMEOUT_MICROS=1000000;
constexpr CanardPortID FOC_COMMAND_STATUS_PORT=3810;
uint64_t now_us=1000;
uint64_t micros_64(){return now_us;}
using InterfacePtr=int;
template<class T> struct AbstractSubscription {
    CanardRxSubscription sub{};
    AbstractSubscription(int,CanardPortID){}
    virtual void handler(const T&,CanardRxTransfer*)=0;
    virtual void accept(CanardRxTransfer* transfer){
        T msg{};size_t size=transfer->payload_size;
        if constexpr(std::is_same_v<T,voltbro_foc_command_2_0>)
            voltbro_foc_command_2_0_deserialize_(&msg,static_cast<const uint8_t*>(transfer->payload),&size);
        else voltbro_foc_sync_2_0_deserialize_(&msg,static_cast<const uint8_t*>(transfer->payload),&size);
        handler(msg,transfer);
    }
};
struct FOCTarget {float torque{},angle{},velocity{},angle_kp{},velocity_kp{};};
enum class VBDriveStatus{NOMINAL,COOLING_DOWN};
struct Motor {
    bool on=true,valid=true;
    VBDriveStatus status=VBDriveStatus::NOMINAL;
    FOCTarget target{};
    float kp=4,ki=1600;
    unsigned updates=0,targets=0;
    bool is_on(){return on;}
    VBDriveStatus get_status(){return status;}
    bool is_foc_point_valid(const FOCTarget& t){return valid && std::fabs(t.torque)<=5;}
    bool set_foc_point(FOCTarget t){if(!is_foc_point_valid(t))return false;target=t;++targets;return true;}
    void set_current_regulator_params(float p,float i){kp=p;ki=i;}
    void update(){++updates;}
} device;
auto get_motor(){return &device;}
struct Manager {
    bool running=true;
    bool is_app_running(){return running;}
    int set_motor_enabled(bool enabled,MotorDisableReason=MotorDisableReason::RegisterStop){device.on=enabled;if(!enabled){device.target={};reset_foc_cycle_session();}return 0;}
    static void reset_foc_cycle_session();
} manager;
auto& get_app_manager(){return manager;}
bool serial_deferred=false;
unsigned profile_main_begin(){return 0;}
void profile_main_end(unsigned){}
unsigned errors=0;
void record_invalid_command(){++errors;}
FocCycleSync foc_cycle_sync{SyncMode::Synchronized};
MotorDisableDiagnostics motor_disable_diagnostics;
struct Registers {uint32_t TXBRP=0;} regs;
struct Fdcan {Registers* Instance=&regs;} hfdcan1;
constexpr unsigned FDCAN_TXBRP_TRP=7;
unsigned free_slots=3,aborted=0;
unsigned HAL_FDCAN_GetTxFifoFreeLevel(Fdcan*){return free_slots;}
int HAL_FDCAN_AbortTxRequest(Fdcan*,unsigned mask){aborted=mask;free_slots=3;return 0;}
struct Sink {
    std::vector<voltbro_foc_command_status_1_0> statuses;
    void send_msg(voltbro_foc_command_status_1_0* msg,unsigned port,CanardTransferID*,uint64_t,CanardPriority priority){
        assert(port==3810 && priority==CanardPriorityImmediate);
        if(msg->status==3)assert(!device.on);
        statuses.push_back(*msg);
    }
    void process_canard_tx(bool all){assert(!all);}
} sink;
auto get_interface(){return &sink;}
bool _is_cyphal_on=true;
void publish_foc_statuses();
'''
handlers = interface[interface.index('class FOCCommandSub:'):interface.index('class ServoSub:')]
helpers = '\n'.join(function(app, name) for name in (
    'void reset_foc_cycle_session(', 'void service_foc_watchdog('))
status = function(interface, 'void publish_command_status(')
status_drain = function(interface, 'void publish_foc_statuses(')
program = prefix + status + status_drain + helpers + '\nvoid Manager::reset_foc_cycle_session(){::reset_foc_cycle_session();}\n' + handlers
program += function(app, 'extern "C" __attribute__((hot, optimize("O3"))) void main_callback(')
program += r'''
void stage(FOCCommandSub& sub,uint16_t cycle,uint8_t source=42){
    voltbro_foc_command_2_0 cmd{};cmd.cycle_id=cycle;
    cmd._torque.newton_meter=1;cmd.angle.radian=.5f;
    cmd.I_kp.value=3;cmd.I_ki.value=400;
    CanardRxTransfer transfer{};transfer.timestamp_usec=now_us;transfer.metadata.remote_node_id=source;
    sub.handler(cmd,&transfer);
}
void marker(FOCSyncSub& sub,uint16_t cycle,uint8_t phase,uint8_t source=42){
    voltbro_foc_sync_2_0 msg{};msg.cycle_id=cycle;msg.phase=phase;
    CanardRxTransfer transfer{};transfer.timestamp_usec=now_us;transfer.metadata.remote_node_id=source;
    sub.handler(msg,&transfer);
}
int main(){
    FOCCommandSub command(0,2301);FOCSyncSub sync(0,3809);
    // Commands are staged without changing the active target or PI; STAGED is immediate.
    regs.TXBRP=7;free_slots=0;stage(command,1);
    assert(device.targets==0 && device.kp==4);
    assert(sink.statuses.size()==1 && sink.statuses.back().status==0 && aborted==7);
    main_callback();assert(device.targets==0);
    marker(sync,1,0);now_us+=25;main_callback();
    assert(device.targets==0 && device.kp==4); // PREPARE never changes physical control.
    while(auto status=foc_cycle_sync.pop_status()) {
        if(status->status==StatusCode::Applied)break;
    }
    stage(command,2);marker(sync,2,1);now_us+=25;main_callback();
    assert(device.targets==1 && device.target.torque==1 && device.target.angle==.5f);
    assert(device.kp==3 && device.ki==400);
    std::optional<CommandStatus> ack;
    while(auto status=foc_cycle_sync.pop_status())if(status->status==StatusCode::Applied)ack=status;
    assert(ack && ack->status==StatusCode::Applied);
    marker(sync,2,1);main_callback();assert(device.targets==1);
    // Master validation covers COMMAND as well as SYNC, so another publisher cannot inject a target.
    reset_foc_cycle_session();const auto before=sink.statuses.size();
    stage(command,2,43);marker(sync,2,1,43);main_callback();
    assert(device.targets==1 && sink.statuses.size()==before);
    // DSDL implicit zero-extension must not turn a truncated/old command into a new target.
    uint8_t wire[30]{};voltbro_foc_command_2_0 msg{};msg.cycle_id=30;
    size_t size=sizeof(wire);assert(voltbro_foc_command_2_0_serialize_(&msg,wire,&size)==0);
    CanardRxTransfer short_transfer{};short_transfer.payload=wire;short_transfer.payload_size=20;
    short_transfer.metadata.remote_node_id=42;short_transfer.timestamp_usec=now_us;
    command.accept(&short_transfer);
    assert(sink.statuses.size()==before && !foc_cycle_sync.consume_armed(now_us));
    // OFF, maintenance and thermal fault cannot retain a command for later replay.
    for(unsigned reason=0;reason<3;++reason){
        reset_foc_cycle_session();stage(command,3);marker(sync,3,1);
        if(reason==0)device.on=false;
        if(reason==1)manager.running=false;
        if(reason==2)device.status=VBDriveStatus::COOLING_DOWN;
        main_callback();
        device.on=true;manager.running=true;device.status=VBDriveStatus::NOMINAL;
        main_callback();assert(device.targets==1);
    }
    // Invalidated limits at apply time fail closed and report rejection instead of APPLIED.
    reset_foc_cycle_session();stage(command,4);marker(sync,4,1);device.valid=false;
    main_callback();assert(!device.on && device.targets==1);
    device.valid=true;
    // Watchdog clears enabled intent through the state controller, not a raw motor stop.
    reset_foc_cycle_session();device.on=true;stage(command,5);marker(sync,5,1);main_callback();
    now_us+=50001;service_foc_watchdog();assert(!device.on);
    bool timeout_reported=false;
    for(const auto& status:sink.statuses)timeout_reported|=(status.status==3 && status.reason==8);
    assert(timeout_reported);
    reset_foc_cycle_session();device.on=true;sink.statuses.clear();
    marker(sync,300,1);_is_cyphal_on=false;now_us+=50001;
    service_foc_watchdog();assert(!device.on && sink.statuses.empty());
    _is_cyphal_on=true;
    device.on=true;main_callback();assert(device.targets==2);
    // Rejected traffic during a thermal trip must not create a new watchdog session
    // that cancels the upstream neutral recovery after cooling.
    reset_foc_cycle_session();device.on=false;device.status=VBDriveStatus::COOLING_DOWN;
    marker(sync,99,1);
    device.status=VBDriveStatus::NOMINAL;device.on=true;now_us+=50001;
    service_foc_watchdog();assert(device.on);
    // A full status queue must not hide the watchdog's final fail-closed reason.
    reset_foc_cycle_session();device.on=true;sink.statuses.clear();
    marker(sync,200,1);
    for(unsigned i=0;i<7;++i)foc_cycle_sync.stage(CycleCommand{.cycle_id=uint16_t(201+i)},false,now_us);
    now_us+=50001;service_foc_watchdog();assert(!device.on);
    timeout_reported=false;
    for(const auto& status:sink.statuses)timeout_reported|=(status.status==3 && status.reason==8);
    assert(timeout_reported);
    // Actual generated serializers and libcanard reassembly reach the production accept()/handler()/IRQ.
    for(size_t mtu : {8,12,16,20,24,32,48,64}) {
        reset_foc_cycle_session();device=Motor{};
        auto alloc=+[](CanardInstance*,size_t n){return std::malloc(n);};
        auto release=+[](CanardInstance*,void* p){std::free(p);};
        auto tx=canardInit(alloc,release),rx=canardInit(alloc,release);
        tx.node_id=42;rx.node_id=1;
        auto queue=canardTxInit(32,mtu);
        CanardRxSubscription command_sub{},sync_sub{};
        assert(canardRxSubscribe(&rx,CanardTransferKindMessage,2301,30,0,&command_sub)>=0);
        assert(canardRxSubscribe(&rx,CanardTransferKindMessage,3809,3,0,&sync_sub)>=0);
        auto send=[&](unsigned port,uint8_t* bytes,size_t size){
            CanardTransferMetadata meta{CanardPriorityImmediate,CanardTransferKindMessage,
                static_cast<CanardPortID>(port),CANARD_NODE_ID_UNSET,0};
            assert(canardTxPush(&queue,&tx,now_us+1000000,&meta,size,bytes)>0);
            unsigned received=0;
            while(auto* item=canardTxPeek(&queue)) {
                CanardRxTransfer transfer{};
                const int result=canardRxAccept(&rx,now_us,&item->frame,0,&transfer,nullptr);
                assert(result>=0);
                if(result==1) {
                    ++received;
                    if(port==2301)command.accept(&transfer);else sync.accept(&transfer);
                    release(&rx,transfer.payload);
                }
                release(&tx,canardTxPop(&queue,item));
            }
            assert(received==1);
        };
        voltbro_foc_command_2_0 cmd{};cmd.cycle_id=7;cmd._torque.newton_meter=1;
        cmd.I_kp.value=3;cmd.I_ki.value=400;
        uint8_t command_bytes[30]{};size_t command_size=30;
        assert(voltbro_foc_command_2_0_serialize_(&cmd,command_bytes,&command_size)==0);
        send(2301,command_bytes,command_size);main_callback();assert(device.targets==0);
        voltbro_foc_sync_2_0 run{};run.cycle_id=7;run.phase=1;
        uint8_t marker_bytes[3]{};size_t marker_size=3;
        assert(voltbro_foc_sync_2_0_serialize_(&run,marker_bytes,&marker_size)==0);
        send(3809,marker_bytes,marker_size);now_us+=25;main_callback();
        assert(device.targets==1 && device.target.torque==1 && device.kp==3 && device.ki==400);
        canardRxUnsubscribe(&rx,CanardTransferKindMessage,2301);
        canardRxUnsubscribe(&rx,CanardTransferKindMessage,3809);
    }
    // Characterize the overlap: two meaningful old fields can occupy exactly
    // the bytes seen as a new 30-byte command after CAN FD padding/extent truncation.
    // A source/size check alone is not a safe version discriminator.
    auto alloc=+[](CanardInstance*,size_t n){return std::malloc(n);};
    auto release=+[](CanardInstance*,void* p){std::free(p);};
    auto tx=canardInit(alloc,release),rx=canardInit(alloc,release);
    tx.node_id=42;rx.node_id=1;
    auto queue=canardTxInit(8,64);CanardRxSubscription subscription{};
    assert(canardRxSubscribe(&rx,CanardTransferKindMessage,2108,30,0,&subscription)>=0);
    uint8_t legacy[28]{};legacy[0]=1;
    CanardTransferMetadata metadata{CanardPriorityImmediate,CanardTransferKindMessage,2108,CANARD_NODE_ID_UNSET,0};
    assert(canardTxPush(&queue,&tx,now_us+1000000,&metadata,sizeof(legacy),legacy)>0);
    auto* item=canardTxPeek(&queue);CanardRxTransfer transfer{};
    assert(canardRxAccept(&rx,now_us,&item->frame,0,&transfer,nullptr)==1);
    assert(transfer.payload_size==30); // 28-byte legacy data is indistinguishable by length.
    release(&rx,transfer.payload);release(&tx,canardTxPop(&queue,item));
    canardRxUnsubscribe(&rx,CanardTransferKindMessage,2108);
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-sync-app-') as directory:
    path = Path(directory)
    (path/'test.cpp').write_text(program)
    lib = root/'Drivers/libcxxcanard/libs/libcanard'
    subprocess.run(['cc','-std=c11','-I'+str(lib),'-c',str(lib/'canard.c'),
                    '-o',str(path/'canard.o')],check=True)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'App'),'-I'+str(root/'Drivers/libcxxcanard/libs'),
                    '-I'+str(root/'build/Release/cyphal_types/c'),str(path/'test.cpp'),
                    str(root/'App/foc_cycle_sync.cpp'),str(path/'canard.o'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS: production synchronized COMMAND/PREPARE/RUN/IRQ, master gate, OFF, fault and watchdog')

#!/usr/bin/env python3
"""Execute production subscription setup and verify CAN filter destinations."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
interface=(root/'App/communications/cyphal/interface.hpp').read_text()
start=interface.index('void setup_subscriptions() {')
setup=interface[start:]
source=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <libcanard/canard.h>
#define HAL_IMPORTANT(expr) assert((expr)==0);
__PRODUCTION_PORTS__
constexpr unsigned FDCAN_REJECT=0,FDCAN_REJECT_REMOTE=0,FDCAN_FILTER_TO_RXFIFO1=1,FDCAN_FILTER_TO_RXFIFO0=0;
constexpr unsigned VBDRIVE_VERSION_MAJOR=5,VBDRIVE_VERSION_MINOR=0,VBDRIVE_VCS_REVISION_ID=0;
struct Version{int major,minor;};
using uavcan_node_Version_1_0=Version;
struct Config {struct App{unsigned sync_mode=1;} app;};
struct Manager{Config config;unsigned get_node_id(){return 1;}Config& get_committed_config(){return config;}} manager;
auto& get_app_manager(){return manager;}
enum class SyncMode{Immediate,Synchronized};
struct Sync {void set_mode(SyncMode){} void set_applied_retry_slot(unsigned){}} foc_cycle_sync;
int hfdcan1;
int HAL_FDCAN_ConfigGlobalFilter(int*,unsigned,unsigned,unsigned,unsigned){return 0;}
struct Catalog {constexpr unsigned size()const{return 60;}} PARAMETER_CATALOG;
int PARAMETER_NAMES;
void handle_parameter_register(){}
template<unsigned>struct StaticRegisters {int names;void(*handler)();};
template<class T> struct Reserved {
 T value;
 template<class... A>void create(A... args){value.create(args...);}
 T* operator->(){return &value;}
};
struct Subscriber {
 unsigned port=0;
 void create(int,unsigned id){port=id;}
 unsigned make_filter(unsigned){return port;}
};
struct Registers {
 template<class A>void create(A,int){}
 unsigned make_filter(unsigned){return 9001;}
};
struct NodeInfo {
 template<class... A>void create(A...){}
 void set_image_crc(uint64_t){}
 unsigned make_filter(unsigned){return 9002;}
};
Reserved<Registers> registers_handler;
Reserved<NodeInfo> node_info_reader;
Reserved<Subscriber> foc_sync_sub,servo_sub,foc_command_sub;
int get_interface(){return 0;}
uint64_t firmware_crc64(const uint8_t*,unsigned){return 123;}
struct Filter{unsigned index,id,fifo;};
std::vector<Filter> filters;
int apply_filter(unsigned index,int*,unsigned id,unsigned fifo=0){filters.push_back({index,id,fifo});return 0;}
''' + setup + r'''
int main(){
 setup_subscriptions();
 assert(foc_command_sub->port==2301 && foc_sync_sub->port==3809 && servo_sub->port==3408);
 assert(filters.size()==5);
 assert(filters[0].id==2301 && filters[0].fifo==1);
 assert(filters[3].id==3809 && filters[3].fifo==1);
 for(const auto& f:filters)assert(f.id!=2108);
 // Verify the actual libcanard bitmask does not admit the legacy subject.
 const auto current=canardMakeFilterForSubject(foc_command_sub->port);
 const auto legacy=canardMakeFilterForSubject(2108);
 assert((current.extended_can_id & current.extended_mask) !=
        (legacy.extended_can_id & current.extended_mask));
 // This board's new subject must not collide with a legacy node anywhere in the
 // supported node-ID range, not merely the legacy ID of this same board.
 for(unsigned new_node=1;new_node<=127;++new_node)
  for(unsigned old_node=1;old_node<=127;++old_node) {
   const auto fresh=canardMakeFilterForSubject(FOC_COMMAND_PORT+new_node);
   const auto old=canardMakeFilterForSubject(2107+old_node);
   assert((fresh.extended_can_id & fresh.extended_mask) !=
          (old.extended_can_id & fresh.extended_mask));
   const CanardFilter other_filters[]{
    canardMakeFilterForSubject(FOC_SYNC_PORT),
    canardMakeFilterForSubject(SERVO_PORT+new_node),
    canardMakeFilterForService(384,new_node),
    canardMakeFilterForService(430,new_node)};
   for(const auto& f:other_filters)
    assert((f.extended_can_id & f.extended_mask) != (old.extended_can_id & f.extended_mask));
  }
}
'''
ports='\n'.join(line for line in interface.splitlines() if line.startswith('static constexpr CanardPortID '))
source=source.replace('__PRODUCTION_PORTS__',ports)
with tempfile.TemporaryDirectory(prefix='vbdrive-subjects-') as d:
    p=Path(d);(p/'test.cpp').write_text(source)
    lib=root/'Drivers/libcxxcanard/libs/libcanard'
    subprocess.run(['cc','-std=c11','-I'+str(lib),'-c',str(lib/'canard.c'),'-o',str(p/'canard.o')],check=True)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(lib.parent),str(p/'test.cpp'),str(p/'canard.o'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: command2 uses 2300+node and is disjoint from every legacy node; sync/Servo preserved')

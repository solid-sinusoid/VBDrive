#!/usr/bin/env python3
"""Exercise real application handoff and VBBoot transport unpacking offline."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]

def function(text, signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

app=(root/'App/app.cpp').read_text()
boot=(root/'VBBoot/App/app.c').read_text()
source=r'''
#include <cassert>
#include <cstdint>
#include "voltbro/config/base_config.h"
#define BOOT_REQUEST_MAGIC 0xB00710ADUL
#define BOOT_CFG_NODE_ID_MASK 0x7FFUL
#define BOOT_CFG_NOMINAL_SHIFT 11U
#define BOOT_CFG_DATA_SHIFT 19U
#define BOOT_CFG_FD_MODE_BIT 27U
#define BOOT_CFG_BITRATE_SWITCH_BIT 28U
struct FOCTarget{float torque;};
struct Motor{bool on=true;void set_foc_point(FOCTarget){}void stop(){on=false;}} motor;
auto get_motor(){return &motor;}
struct Config{BaseConfigData base{};};
struct Manager{Config config;uint8_t get_node_id(){return config.base.node_id;}
 const Config& get_committed_config(){return config;}} manager;
auto& get_app_manager(){return manager;}
struct Tamp{uint32_t BKP0R=0,BKP1R=0,BKP2R=1;} tamp;
#define TAMP (&tamp)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
void HAL_PWR_EnableBkUpAccess(){}void HAL_PWR_DisableBkUpAccess(){}
void HAL_Delay(int){}unsigned resets=0;void NVIC_SystemReset(){++resets;}
int hfdcan1;int HAL_FDCAN_Stop(int*){return 0;}
struct BootTransportConfig{uint32_t can_id;uint8_t nominal_prescaler,data_prescaler;bool fd_mode,bitrate_switch;};
''' + function(app,'void reboot_to_bootloader()') + '\n' + function(boot,'static void boot_request_unpack(') + r'''
int main(){
 for(unsigned nominal=0;nominal<=4;++nominal)for(unsigned data=0;data<=3;++data){
  manager.config.base.node_id=11;manager.config.base.fdcan_nominal_baud=nominal;
  manager.config.base.fdcan_data_baud=data;motor.on=true;
  reboot_to_bootloader();assert(!motor.on && tamp.BKP0R==BOOT_REQUEST_MAGIC && tamp.BKP2R==0);
  BootTransportConfig out{};boot_request_unpack(tamp.BKP1R,&out);
  assert(out.can_id==11 && out.nominal_prescaler==voltbro_can_nominal_prescaler(nominal));
  assert(out.data_prescaler==voltbro_can_data_prescaler(data) && out.fd_mode && out.bitrate_switch);
 }
 assert(resets==20);
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-boot-request-') as d:
    p=Path(d);(p/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-I'+str(root/'Drivers/libvoltbro'),
                    str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: actual app handoff and VBBoot unpack preserve node ID and all valid CAN baud settings')

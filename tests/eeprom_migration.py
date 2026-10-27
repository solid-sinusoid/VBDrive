#!/usr/bin/env python3
"""Prove the startup EEPROM guard preserves unknown and overlapping legacy images."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include "config/config.hpp"
class EEPROM {
public:
 std::array<uint8_t,32768> bytes{};
 unsigned writes=0;
 bool fail=false;
 void wait_until_available(){}
 template<class T> int read(T* out,uint16_t address){
  if(fail || address+sizeof(T)>bytes.size())return 1;
  memcpy(out,bytes.data()+address,sizeof(T));return 0;
 }
 template<class T> int write(T* in,uint16_t address){
  ++writes;memcpy(bytes.data()+address,in,sizeof(T));return 0;
 }
};
__PRODUCTION_CALIBRATION__
static_assert(offsetof(CalibrationData,type_id)==8 && sizeof(CalibrationData)==8204);
struct InductiveSensor {struct State {static constexpr uint32_t TYPE_ID=0xAAAAAA99;};};
#include "eeprom_guard.hpp"
EEPROM eeprom;
bool warned=false;
void serial_send_message(const char*){warned=true;}
void HAL_Delay(int){throw std::runtime_error("blocked");}
__PRODUCTION_STARTUP_GUARD__
int main(){
 EEPROM e;e.bytes.fill(0xff);
 assert(eeprom_layout_safe(e));assert(e.writes==0);
 // A historical calibration starts at zero and overlaps the new config.
 CalibrationData legacy;legacy.reset();legacy.was_calibrated=true;
 legacy.type_id=0x89ABCDEF;memcpy(e.bytes.data(),&legacy,sizeof(legacy));
 auto before=e.bytes;assert(!eeprom_layout_safe(e));assert(e.bytes==before && e.writes==0);
 // Blank config cannot grant permission to touch an unknown pre-existing sensor record.
 e.bytes.fill(0xff);uint32_t unknown=0x1234;memcpy(e.bytes.data()+0x2200,&unknown,4);
 before=e.bytes;assert(!eeprom_layout_safe(e));assert(e.bytes==before && e.writes==0);
 // A valid current config and sensor state pass, but an incompatible calibration doesn't.
 e.bytes.fill(0xff);DriveConfig cfg{};cfg.base=BASE_CONFIG_DEFAULTS;
 cfg.base.node_id=1;cfg.app.gear_ratio=36;memcpy(e.bytes.data(),&cfg,sizeof(cfg));
 uint32_t sensor=0xAAAAAA99;memcpy(e.bytes.data()+0x2200,&sensor,4);
 e.bytes[0x2204]=1;
 CalibrationData calibration;calibration.reset();calibration.was_calibrated=true;
 memcpy(e.bytes.data()+0x100,&calibration,sizeof(calibration));
 assert(eeprom_layout_safe(e));
 before=e.bytes;
 uint32_t wrong=0x89ABCDEF;
 memcpy(e.bytes.data()+0x100+offsetof(CalibrationData,type_id),&wrong,4);
 assert(!eeprom_layout_safe(e));assert(e.writes==0);
 e.bytes=before;
 // The firmware writes this record only after successful programming; a
 // persisted false flag must not cause automatic reprogramming of the sensor.
 e.bytes[0x2204]=0;before=e.bytes;
 assert(!eeprom_layout_safe(e));assert(e.bytes==before && e.writes==0);
 e.bytes[0x2204]=1;
 // A newly provisioned config must survive reset before its first sensor initialization.
 e.bytes.fill(0xff);memcpy(e.bytes.data(),&cfg,sizeof(cfg));
 assert(eeprom_layout_safe(e));
 // But a config alone must not conceal any older calibration/configuration data.
 e.bytes[0x200d]=0;assert(!eeprom_layout_safe(e));
 e.bytes.fill(0xff);memcpy(e.bytes.data(),&cfg,sizeof(cfg));
 memcpy(e.bytes.data()+0x2200,&sensor,4);e.bytes[0x2204]=1;
 memcpy(e.bytes.data()+0x100,&legacy,sizeof(legacy));before=e.bytes;
 assert(!eeprom_layout_safe(e));assert(e.bytes==before && e.writes==0);
 e.fail=true;assert(!eeprom_layout_safe(e));
 eeprom.bytes.fill(0xff);memcpy(eeprom.bytes.data(),&legacy,sizeof(legacy));
 before=eeprom.bytes;
 try {startup_guard();assert(false);} catch(const std::runtime_error&){}
 assert(warned && eeprom.writes==0 && eeprom.bytes==before);
}
'''
app=(root/'App/app.cpp').read_text()
calibration=(root/'Drivers/libvoltbro/voltbro/motors/bldc/foc/foc.hpp').read_text()
source=source.replace('__PRODUCTION_CALIBRATION__',calibration[calibration.index('#define USE_CALIBRATION_ARRAY'):calibration.index('struct FOCTarget')])
begin=app.index('    eeprom.wait_until_available();',app.index('void app()'))
end=app.index('    auto& app_manager =',begin)
source=source.replace('__PRODUCTION_STARTUP_GUARD__','void startup_guard(){\n'+app[begin:end]+'}\n')
with tempfile.TemporaryDirectory(prefix='vbdrive-eeprom-') as d:
    p=Path(d);(p/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++20','-I'+str(root/'App'),'-I'+str(root/'Drivers/libvoltbro'),
                    str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS: unknown/legacy EEPROM and sensor state fail closed without writes')

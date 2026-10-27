#!/usr/bin/env python3
"""Execute the real calibration transport pause/resume with both hardware FIFOs."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
text=(root/'App/communications/cyphal/setup.hpp').read_text()
code=text[text.index('void pause_cyphal_for_calibration()'):]
program=r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
unsigned pending[2]{3,2},aborted=0,starts=0,stops=0;
constexpr unsigned FDCAN_RX_FIFO0=0,FDCAN_RX_FIFO1=1;
constexpr unsigned FDCAN_TX_BUFFER0=1,FDCAN_TX_BUFFER1=2,FDCAN_TX_BUFFER2=4;
struct FDCAN_RxHeaderTypeDef{};
int hfdcan1;
int HAL_FDCAN_AbortTxRequest(int*,unsigned mask){aborted=mask;return 0;}
int HAL_FDCAN_Stop(int*){++stops;return 0;}
int HAL_FDCAN_Start(int*){++starts;return 0;}
unsigned HAL_FDCAN_GetRxFifoFillLevel(int*,unsigned fifo){return pending[fifo];}
int HAL_FDCAN_GetRxMessage(int*,unsigned fifo,FDCAN_RxHeaderTypeDef*,uint8_t*){
 assert(pending[fifo]>0);--pending[fifo];return 0;
}
struct Interface {unsigned halts=0,resets=0;void halt(){++halts;}void reset(){++resets;}} transport;
auto cyphal_interface=&transport;
bool _is_cyphal_on=true;
unsigned delay_cyphal_until_millis=123;
#define HAL_IMPORTANT(expr) assert((expr)==0);
''' + code + r'''
int main(){
 pause_cyphal_for_calibration();
 assert(!_is_cyphal_on && delay_cyphal_until_millis==0 && aborted==7 && stops==1 && transport.halts==1);
 resume_cyphal_after_calibration();
 assert(_is_cyphal_on && transport.resets==1 && starts==1 && pending[0]==0 && pending[1]==0);
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-cyphal-lifecycle-') as directory:
    path=Path(directory)
    (path/'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS: calibration aborts TX, reuses arena, flushes regular and critical RX before resuming')

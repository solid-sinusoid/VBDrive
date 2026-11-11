#!/usr/bin/env python3
"""Exercise production G4 receive handling with saturated hardware FIFOs."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def body(signature):
    start = g4.index(signature)
    pos = g4.index('{', start)
    depth, end = 1, pos + 1
    while depth:
        depth += (g4[end] == '{') - (g4[end] == '}')
        end += 1
    return g4[start:end]

prefix = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <vector>
#define STM32G4 1
constexpr uint32_t TIM_SR_UIF=1;
struct Timer {uint32_t CNT=0,SR=0;} timer;
auto* TIM2=&timer;
constexpr uint32_t FDCAN_RX_FIFO0=0, FDCAN_RX_FIFO1=1;
constexpr int HAL_OK=0;
struct CanardFrame {uint32_t extended_can_id=0; unsigned payload_size=0; void* payload=nullptr;};
struct FDCAN_RxHeaderTypeDef {uint32_t Identifier=0,DataLength=0,RxTimestamp=0;};
unsigned pending[2]{},reads=0;
bool saturated=false,fail_read=false;
uint8_t tail=0xe0;
unsigned HAL_FDCAN_GetRxFifoFillLevel(void*,unsigned fifo) {return pending[fifo];}
int HAL_FDCAN_GetRxMessage(void*,unsigned fifo,FDCAN_RxHeaderTypeDef* header,uint8_t* bytes) {
    if(fail_read || ++reads>16) return 1;
    --pending[fifo];
    header->Identifier=100+fifo;
    header->RxTimestamp=321;
    header->DataLength=1;bytes[0]=tail;
    if(saturated) ++pending[fifo];
    return HAL_OK;
}
struct G4CAN {
    void* handler=nullptr;
    struct Utility {unsigned errors=0; void error_handler(){++errors;}} utilities;
    std::vector<unsigned> accepted;
    std::vector<bool> receipt_valid;
    std::vector<uint32_t> receipt_lower;
    uint32_t rx_empty_tick[2]{};
    bool rx_empty_known[2]{};
    uint32_t rx_received_after_tick=0;
    bool rx_receipt_valid=false;
    uint32_t rx_hardware_timestamp=0;
    void accept_rx_frame(const CanardFrame& frame){
        accepted.push_back(frame.extended_can_id);
        receipt_valid.push_back(rx_receipt_valid);
        receipt_lower.push_back(rx_received_after_tick);
    }
    unsigned dlc_to_len(unsigned dlc){return dlc;}
    bool read_frame(CanardFrame*,void*);
    bool read_frame_from_fifo(CanardFrame*,void*,uint32_t);
    void process_canard_rx(bool all=true);
};
'''
suffix = r'''
int main() {
    G4CAN g;
    // Critical messages must progress even with no regular traffic.
    pending[0]=0;pending[1]=3;g.process_canard_rx();
    assert((g.accepted==std::vector<unsigned>{101,101,101}));
    // Replenishing FIFOs must not starve the application main loop.
    g.accepted.clear();reads=0;pending[0]=3;pending[1]=3;saturated=true;
    g.process_canard_rx();
    assert((g.accepted==std::vector<unsigned>{101,101,101,100}));
    assert(pending[0]==3 && pending[1]==3);
    // Preserve the upstream single-frame API; prefer the critical FIFO.
    g.accepted.clear();reads=0;g.process_canard_rx(false);
    assert((g.accepted==std::vector<unsigned>{101}));
    g.accepted.clear();pending[1]=0;g.process_canard_rx(false);
    assert((g.accepted==std::vector<unsigned>{100}));
    // HAL failures must never deliver uninitialized or previous frame data.
    g.accepted.clear();fail_read=true;g.process_canard_rx(false);
    assert(g.accepted.empty() && g.utilities.errors==1);
    fail_read=false;saturated=false;pending[0]=pending[1]=0;
    g.process_canard_rx();assert(g.accepted.empty());
    pending[1]=1;reads=0;
    g.process_canard_rx();
    assert(g.rx_hardware_timestamp==321);
    // A nonempty FIFO at startup has no known lower arrival bound.
    G4CAN unknown;pending[1]=1;pending[0]=0;reads=0;
    unknown.process_canard_rx();
    assert(!unknown.receipt_valid.back());
    // Only an observed empty FIFO establishes a bound, not a drain invocation.
    G4CAN bounded;timer.CNT=100;pending[0]=pending[1]=0;
    bounded.process_canard_rx();
    timer.CNT=116;pending[1]=1;reads=0;
    bounded.process_canard_rx();
    assert(bounded.receipt_valid.back());
    assert(bounded.receipt_lower.back()==100);
    // Continuously replenished traffic must not silently renew the bound.
    saturated=true;timer.CNT=1000;pending[1]=3;reads=0;
    bounded.process_canard_rx();
    assert(bounded.receipt_lower.back()==100);
    timer.CNT=10600;reads=0;bounded.process_canard_rx();
    assert(bounded.receipt_lower.back()==100);
    // Caller sees 1.05 s upper age; no modulo16 timestamp alias to 1.6 ms.
    assert(timer.CNT-bounded.receipt_lower.back()==10500);
    // Pending full timer wrap invalidates a previously known bound.
    timer.SR=TIM_SR_UIF;timer.CNT=4;reads=0;
    bounded.process_canard_rx();
    assert(!bounded.receipt_valid.back());
    timer.SR=0;reads=0;bounded.process_canard_rx();
    assert(!bounded.receipt_valid.back());
    saturated=false;pending[0]=pending[1]=0;timer.CNT=20;
    bounded.process_canard_rx();
    pending[1]=1;timer.CNT=22;reads=0;bounded.process_canard_rx();
    assert(bounded.receipt_valid.back() && bounded.receipt_lower.back()==20);
    tail=0x60;pending[1]=1;reads=0;bounded.process_canard_rx();
    assert(!bounded.receipt_valid.back()); // Last fragment cannot date the whole transfer.
}
'''

for implementation in ('cyphal/providers/G4CAN.cpp', 'src/cyphal.cpp'):
    g4 = (root / 'Drivers/libcxxcanard' / implementation).read_text()
    source = prefix + '\n'.join(body(signature) for signature in (
        'void G4CAN::process_canard_rx(',
        'bool G4CAN::read_frame(',
        'bool G4CAN::read_frame_from_fifo(',
    )) + suffix
    with tempfile.TemporaryDirectory(prefix='vbdrive-rx-') as directory:
        path = Path(directory)
        (path / 'test.cpp').write_text(source)
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('PASS: critical FIFO priority, bounded saturated receive, single-frame API and HAL errors')

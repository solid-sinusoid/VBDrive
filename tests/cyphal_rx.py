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
constexpr uint32_t FDCAN_RX_FIFO0=0, FDCAN_RX_FIFO1=1;
constexpr int HAL_OK=0;
struct CanardFrame {uint32_t extended_can_id=0; unsigned payload_size=0; void* payload=nullptr;};
struct FDCAN_RxHeaderTypeDef {uint32_t Identifier=0,DataLength=0;};
unsigned pending[2]{},reads=0;
bool saturated=false,fail_read=false;
unsigned HAL_FDCAN_GetRxFifoFillLevel(void*,unsigned fifo) {return pending[fifo];}
int HAL_FDCAN_GetRxMessage(void*,unsigned fifo,FDCAN_RxHeaderTypeDef* header,uint8_t*) {
    if(fail_read || ++reads>16) return 1;
    --pending[fifo];
    header->Identifier=100+fifo;
    if(saturated) ++pending[fifo];
    return HAL_OK;
}
struct G4CAN {
    void* handler=nullptr;
    struct Utility {unsigned errors=0; void error_handler(){++errors;}} utilities;
    std::vector<unsigned> accepted;
    void accept_rx_frame(const CanardFrame& frame){accepted.push_back(frame.extended_can_id);}
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

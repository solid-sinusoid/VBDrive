"""Preserve float comparison boundaries without software-double work on Cortex-M4."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
include = root / 'Drivers/libvoltbro'
source = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include "voltbro/utils.hpp"
extern "C" bool compare_float(float a, float b) { return is_close(a,b); }
#ifndef ARM_PROBE
bool original(float a,float b) { return fabsf(a-b)<1e-10; }
int main() {
    const float edge=1e-10f;
    assert(double(edge)>1e-10);
    for(float x:{0.f,-0.f,edge,-edge,std::nextafter(edge,0.f),
                 std::nextafter(edge,INFINITY),INFINITY,-INFINITY,NAN}) {
        assert(compare_float(x,0)==original(x,0));
        assert(compare_float(x,x)==original(x,x));
    }
    uint32_t seed=42;
    for(unsigned i=0;i<1000000;++i) {
        seed=seed*1664525U+1013904223U;
        float x;std::memcpy(&x,&seed,4);
        assert(compare_float(x,0)==original(x,0));
        assert(compare_float(x,1)==original(x,1));
    }
}
#endif
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    cpp = tmp / 'probe.cpp'
    cpp.write_text(source)
    subprocess.run(['g++','-std=c++17','-O2','-I'+str(include),str(cpp),'-o',str(tmp/'host')],check=True)
    subprocess.run([str(tmp/'host')],check=True)
    subprocess.run(['arm-none-eabi-g++','-std=c++17','-O3','-mcpu=cortex-m4',
                    '-mfpu=fpv4-sp-d16','-mfloat-abi=hard','-DARM_PROBE',
                    '-I'+str(include),'-c',str(cpp),'-o',str(tmp/'probe.o')],check=True)
    symbols = subprocess.check_output(['arm-none-eabi-nm','-u',str(tmp/'probe.o')],text=True)
    assert '__aeabi_f2d' not in symbols and '__aeabi_dcmp' not in symbols, symbols
print('PASS: comparison boundaries/random float inputs preserved; no ARM software-double calls')

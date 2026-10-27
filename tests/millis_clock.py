#!/usr/bin/env python3
"""Production TIM7 polling clock: do not count a tick already serviced by an IRQ."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'App/app.cpp').read_text();start=s.index('millis millis_32() {');end=s.index('\n}',start)+2
function=s[start:end]
micros_start=s.index('micros __attribute__')
micros_function=s[micros_start:s.index('\n}',micros_start)+2]
u=(root/'Drivers/libvoltbro/voltbro/utils.hpp').read_text();macro=u[u.index('#define CRITICAL_SECTION'):u.index('// TODO: add optional warning')]
program=r'''
#include <cassert>
#include <cstdint>
using millis=uint32_t;
volatile millis millis_k=0;
int htim7;
constexpr int TIM_FLAG_UPDATE=1,RESET=0;
bool flag=false,interleave=false;
unsigned mask=0;
unsigned __get_PRIMASK(){return mask;}
void __disable_irq(){mask=1;}
void __set_PRIMASK(unsigned m){mask=m;}
bool get_flag(){
 bool observed=flag;
 if(interleave){assert(mask==0);interleave=false;flag=false;millis_k=millis_k+1;}
 return observed;
}
#define __HAL_TIM_GET_FLAG(timer,bit) get_flag()
#define __HAL_TIM_CLEAR_FLAG(timer,bit) flag=false
'''+macro+function+r'''
int main(){
 millis_k=100;flag=true;interleave=true;
 assert(millis_32()==101);assert(mask==0);assert(millis_32()==101);
 flag=true;assert(millis_32()==102);assert(!flag && mask==0);
 flag=true;mask=1;assert(millis_32()==103);assert(mask==1);
 millis_k=UINT32_MAX;flag=true;assert(millis_32()==0);assert(mask==1);
}
'''
with tempfile.TemporaryDirectory(prefix='vbdrive-clock-') as d:
 p=Path(d);(p/'test.cpp').write_text(program)
 subprocess.run(['c++','-std=c++20','-O2',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 snapshot=r'''
#include <cassert>
#include <cstdint>
#include "monotonic_micros.hpp"
using micros=uint64_t;
using millis=uint32_t;
volatile uint64_t millis_k=100;
int htim7;
constexpr int TIM_FLAG_UPDATE=1,RESET=0;
bool flag=false;
unsigned mask=0,reads=0;
uint32_t counters[2]{999,0};
unsigned __get_PRIMASK(){return mask;}
void __disable_irq(){mask=1;}
void __set_PRIMASK(unsigned m){mask=m;}
#define __HAL_TIM_GET_FLAG(timer,bit) flag
#define __HAL_TIM_CLEAR_FLAG(timer,bit) flag=false
#define __HAL_TIM_GetCounter(timer) counters[reads++%2]
''' + macro + function + micros_function + r'''
int main(){
 assert(micros_64()==101000 && mask==0);
 counters[0]=3;counters[1]=4;reads=0;flag=true;
 assert(micros_64()==101004 && mask==0);
 millis_k=UINT64_C(4294967296);flag=false;counters[0]=120;counters[1]=121;reads=0;mask=1;
 assert(micros_64()==UINT64_C(4294967296121) && mask==1);
}
'''
 (p/'micros.cpp').write_text(snapshot)
 subprocess.run(['c++','-std=c++20','-O2','-I'+str(root/'App'),str(p/'micros.cpp'),'-o',str(p/'micros')],check=True)
 subprocess.run([str(p/'micros')],check=True)
 irq=(root/'Core/Src/stm32g4xx_it.c').read_text()
 start=irq.index('void TIM7_IRQHandler(void)')
 handler=irq[start:irq.index('\n}',start)+2]
 irq_test=r'''
#include <cassert>
#include <cstdint>
int htim7;
unsigned mask=0,callback_count=0;
unsigned __get_PRIMASK(){return mask;}
void __disable_irq(){mask=1;}
void __set_PRIMASK(unsigned old){mask=old;}
void HAL_TIM_IRQHandler(int*){
 // HAL clears UIF before calling the period callback. TIM1 must not observe
 // a cleared flag together with the previous millisecond epoch.
 assert(mask==1);++callback_count;
}
''' + handler + r'''
int main(){
 TIM7_IRQHandler();assert(mask==0 && callback_count==1);
 mask=1;TIM7_IRQHandler();assert(mask==1 && callback_count==2);
}
'''
 (p/'irq.cpp').write_text(irq_test)
 subprocess.run(['c++','-std=c++20','-O2',str(p/'irq.cpp'),'-o',str(p/'irq')],check=True)
 subprocess.run([str(p/'irq')],check=True)
print('PASS: pending TIM7 tick counted once across IRQ interleaving, PRIMASK preserved and wrap handled')

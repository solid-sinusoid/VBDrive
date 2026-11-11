#!/usr/bin/env python3
"""Test the production TIM2 compare callback with register doubles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'App/app.cpp').read_text()
start = source.index('void HAL_TIM_OC_DelayElapsedCallback(')
opening = source.index('{', start)
depth, end = 1, opening + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
callback = source[start:end]
program = r'''
#include <cassert>
#include <cstdint>
struct Timer { uint32_t CCR1=0, CNT=0; } timer;
Timer* TIM2=&timer;
struct TIM_HandleTypeDef { Timer* Instance; unsigned Channel; };
constexpr unsigned HAL_TIM_ACTIVE_CHANNEL_1=1;
constexpr int LED2_GPIO_Port=0, LED2_Pin=0;
unsigned toggles=0;
void HAL_GPIO_TogglePin(int,int){ ++toggles; }
''' + callback + r'''
int main() {
    TIM_HandleTypeDef t{TIM2,1};
    timer.CCR1=10000; timer.CNT=10001;
    HAL_TIM_OC_DelayElapsedCallback(&t);
    assert(timer.CCR1==20000 && toggles==1);
    timer.CCR1=UINT32_MAX-4999; timer.CNT=UINT32_MAX-4998;
    HAL_TIM_OC_DelayElapsedCallback(&t);
    assert(timer.CCR1==5000 && toggles==2);
    timer.CCR1=10000; timer.CNT=25000;
    HAL_TIM_OC_DelayElapsedCallback(&t);
    assert(timer.CCR1==35000 && toggles==3);
    t.Channel=2; HAL_TIM_OC_DelayElapsedCallback(&t);
    assert(toggles==3);
}
'''
with tempfile.TemporaryDirectory(dir='/tmp/opencode') as directory:
    cpp, exe = Path(directory)/'test.cpp', Path(directory)/'test'
    cpp.write_text(program)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Actual TIM2 LED compare callback: normal, wrap, delay PASS')

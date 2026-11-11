#!/usr/bin/env python3
"""Exercise the actual FDCAN configuration with a fake HAL."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root/'App/communications/cyphal/setup.hpp').read_text()
start = text.index('void configure_fdcan(')
opening = text.index('{', start)
depth, end = 1, opening+1
while depth:
    depth += (text[end]=='{') - (text[end]=='}')
    end += 1
source = r'''
#include <cassert>
#include <cstdint>
#define ENABLE 1
#define DISABLE 0
#define HAL_OK 0
#define FDCAN1 nullptr
#define FDCAN_CLOCK_DIV2 0
#define FDCAN_FRAME_FD_BRS 0
#define FDCAN_MODE_NORMAL 0
#define FDCAN_TX_FIFO_OPERATION 0
#define FDCAN_TIMESTAMP_PRESC_16 15
#define FDCAN_TIMESTAMP_INTERNAL 1
struct FDCAN_HandleTypeDef {
    void* Instance; struct { unsigned ClockDivider,FrameFormat,Mode,AutoRetransmission,
    TransmitPause,ProtocolException,NominalSyncJumpWidth,NominalTimeSeg1,NominalTimeSeg2,
    DataSyncJumpWidth,DataTimeSeg1,DataTimeSeg2,StdFiltersNbr,ExtFiltersNbr,
    TxFifoQueueMode,NominalPrescaler,DataPrescaler; } Init;
};
struct Manager { struct Config { struct Base {int fdcan_nominal_baud=0,fdcan_data_baud=0;} base; } config;
    Config& get_committed_config(){return config;} } manager;
Manager& get_app_manager(){return manager;}
int voltbro_can_nominal_prescaler(int){return 1;}
int voltbro_can_data_prescaler(int){return 1;}
int configured=-1,enabled=-1;
int HAL_FDCAN_Init(FDCAN_HandleTypeDef*){return HAL_OK;}
int HAL_FDCAN_ConfigTimestampCounter(FDCAN_HandleTypeDef*,int p){configured=p;return HAL_OK;}
int HAL_FDCAN_EnableTimestampCounter(FDCAN_HandleTypeDef*,int p){enabled=p;return HAL_OK;}
void Error_Handler(){assert(false);}
''' + text[start:end] + r'''
int main(){FDCAN_HandleTypeDef h{};configure_fdcan(&h);assert(configured==15 && enabled==1);}
'''
with tempfile.TemporaryDirectory(dir='/tmp/opencode') as d:
    cpp,exe=Path(d)/'test.cpp',Path(d)/'test'
    cpp.write_text(source)
    subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

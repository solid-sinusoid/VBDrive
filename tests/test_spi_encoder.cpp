#include <cassert>
#include <cstdlib>
#include "voltbro/encoders/ASxxxx/AS5047P.hpp"
Timer timer;
Timer* TIM2 = &timer;
unsigned faults = 0, after_receive = 0;
bool freeze_tick = false;
void fault() { ++faults; }
int main() {
    SPI_TypeDef peripheral;
    SPI_HandleTypeDef handle{&peripheral, {16}};
    AS5047P encoder{GpioPin{}, &handle, false, 0, fault};
    peripheral.status = TXE | RXNE;
    encoder.update_value();
    assert(encoder.healthy() && encoder.get_value() == 0x1234);
    peripheral.status = TXE;
    timer.CNT = UINT32_MAX;
    encoder.update_value();
    assert(!encoder.healthy() && faults == 1 && encoder.get_value() == 0x1234);
    peripheral.status = TXE | RXNE;
    encoder.update_value();
    assert(!encoder.healthy() && faults == 1 && encoder.get_value() == 0x1234);
}

#include <cassert>
#include <string_view>
#include "voltbro/generics/spi_mixin.hpp"

Timer timer;
Timer* TIM2 = &timer;
unsigned faults = 0;
unsigned after_receive = 0;
bool freeze_tick = false;
void fault() { ++faults; }
struct TestSPI : SPIMixin {
    explicit TestSPI(SPI_HandleTypeDef* spi) : SPIMixin(spi, fault) {}
    using SPIMixin::spi_transmit_command_receive;
    using SPIMixin::spi_transmit_receive_buffer8;
    using SPIMixin::spi_transmit_only;
};
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string_view scenario{argv[1]};
    SPI_TypeDef peripheral;
    SPI_HandleTypeDef handle{&peripheral, {16}};
    TestSPI spi{&handle};
    uint16_t value = 0xbeef;
    if (scenario == "normal") {
        peripheral.status = TXE | RXNE;
        assert(spi.spi_transmit_command_receive(1, value));
        assert(value == 0x1234 && faults == 0);
        return 0;
    }
    if (scenario == "buffer") {
        peripheral.status = TXE | RXNE;
        const uint8_t input[]{1,2}; uint8_t output[2]{};
        assert(spi.spi_transmit_receive_buffer8(input, output, 2) == HAL_OK);
        assert(output[0] == 0x12 && output[1] == 0x12 && faults == 0);
        return 0;
    }
    if (scenario == "only_fail") {
        peripheral.status = 0;
        assert(!spi.spi_transmit_only(1));
        assert(faults == 1);
        return 0;
    }
    if (scenario == "buffer_fail") {
        peripheral.status = TXE;
        const uint8_t input[]{1}; uint8_t output[]{0xaa};
        assert(spi.spi_transmit_receive_buffer8(input, output, 1) == HAL_ERROR);
        assert(output[0] == 0xaa && faults == 1);
        return 0;
    }
    if (scenario == "finish") { peripheral.status = TXE | RXNE; after_receive = BSY; }
    if (scenario == "clock") freeze_tick = true;
    if (scenario == "busy") peripheral.status = BSY;
    else if (scenario == "tx") peripheral.status = 0;
    else if (scenario == "rx") peripheral.status = TXE;
    else if (scenario == "fifo") peripheral.status = FIFO | TXE | RXNE;
    else if (scenario != "finish" && scenario != "clock") return 2;
    timer.CNT = UINT32_MAX;
    assert(!spi.spi_transmit_command_receive(1, value));
    assert(value == 0xbeef && faults == 1);
    peripheral.status = TXE | RXNE;
    assert(!spi.spi_transmit_command_receive(1, value));
    assert(value == 0xbeef && faults == 1);
    return 0;
}

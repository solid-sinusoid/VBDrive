#pragma once
#include "stm32g4xx_hal.h"
constexpr unsigned BSY=1, TXE=2, RXNE=4, FIFO=8;
constexpr unsigned LL_SPI_RX_FIFO_EMPTY=0;
inline unsigned LL_SPI_IsEnabled(SPI_TypeDef*) { return 1; }
inline void LL_SPI_Enable(SPI_TypeDef*) {}
extern bool freeze_tick;
inline void tick() { if (!freeze_tick) TIM2->CNT = TIM2->CNT + 1; }
inline unsigned LL_SPI_IsActiveFlag_BSY(SPI_TypeDef* s) { tick(); return s->status & BSY; }
inline unsigned LL_SPI_IsActiveFlag_TXE(SPI_TypeDef* s) { tick(); return s->status & TXE; }
inline unsigned LL_SPI_IsActiveFlag_RXNE(SPI_TypeDef* s) { tick(); return s->status & RXNE; }
inline unsigned LL_SPI_GetRxFIFOLevel(SPI_TypeDef* s) { tick(); return s->status & FIFO; }
inline unsigned LL_SPI_IsActiveFlag_OVR(SPI_TypeDef*) { return 0; }
inline void LL_SPI_ClearFlag_OVR(SPI_TypeDef*) {}
extern unsigned after_receive;
inline void LL_SPI_TransmitData16(SPI_TypeDef*, uint16_t) {}
inline void LL_SPI_TransmitData8(SPI_TypeDef*, uint8_t) {}
inline uint16_t LL_SPI_ReceiveData16(SPI_TypeDef* s) { if (after_receive) s->status = after_receive; return 0x1234; }
inline uint8_t LL_SPI_ReceiveData8(SPI_TypeDef* s) { if (after_receive) s->status = after_receive; return 0x12; }

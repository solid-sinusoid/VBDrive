#pragma once
#include <atomic>
#include <cstdint>
#define HAL_SPI_MODULE_ENABLED
#define SPI_DATASIZE_8BIT 8
using HAL_StatusTypeDef = int;
constexpr int HAL_OK = 0, HAL_ERROR = 1;
struct SPI_TypeDef { std::atomic<unsigned> status{0}; };
struct SPI_HandleTypeDef { SPI_TypeDef* Instance; struct { int DataSize; } Init; };
struct Timer { std::atomic<unsigned> CNT{0}; };
extern Timer* TIM2;
#define assert_param(x) ((void)0)

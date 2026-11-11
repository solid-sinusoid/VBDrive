#pragma once
#include <cstdint>
#include <type_traits>
#define FORCE_INLINE inline
using encoder_data = uint16_t;
constexpr unsigned CYCLES_100NS_160Mhz = 16;
template<typename E> constexpr auto to_underlying(E e) { return static_cast<std::underlying_type_t<E>>(e); }
inline void delay_cpu_cycles(unsigned) {}

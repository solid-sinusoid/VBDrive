#pragma once
#include <cstddef>
#include <cstdint>

// CRC-64/ECMA-182 of the complete fixed application Flash range, including erased holes.
inline uint64_t firmware_crc64(const uint8_t* bytes, size_t length) {
    uint64_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= uint64_t{bytes[i]} << 56;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc << 1) ^ ((crc >> 63) ? UINT64_C(0x42F0E1EBA9EA3693) : 0);
    }
    return crc;
}

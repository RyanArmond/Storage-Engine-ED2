#pragma once

#include <cstddef>
#include <cstdint>

namespace storage {

/**
 * @brief Computes standard IEEE 802.3 CRC32 checksum.
 */
class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length);
    static uint32_t update(uint32_t crc, const void* data, size_t length);

private:
    static const uint32_t table[256];
};

} // namespace storage

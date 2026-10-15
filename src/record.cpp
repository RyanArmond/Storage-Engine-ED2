#include "record.hpp"
#include "checksum.hpp"
#include <cstring>

namespace storage {

void encode_uint32_le(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>(val & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

uint32_t decode_uint32_le(const uint8_t* src) {
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

void encode_uint64_le(uint8_t* dst, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        dst[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
    }
}

uint64_t decode_uint64_le(const uint8_t* src) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= (static_cast<uint64_t>(src[i]) << (i * 8));
    }
    return val;
}

std::vector<uint8_t> Record::serialize() const {
    uint8_t op_byte = static_cast<uint8_t>(type);
    uint32_t val_sz = (type == OpType::DELETE) ? 0 : static_cast<uint32_t>(value.size());

    // Compute CRC32 over op_byte + encoded key + encoded val_sz + value payload
    uint32_t crc = 0xFFFFFFFF;
    crc = CRC32::update(crc, &op_byte, 1);

    uint8_t key_buf[8];
    encode_uint64_le(key_buf, key);
    crc = CRC32::update(crc, key_buf, 8);

    uint8_t sz_buf[4];
    encode_uint32_le(sz_buf, val_sz);
    crc = CRC32::update(crc, sz_buf, 4);

    if (val_sz > 0) {
        crc = CRC32::update(crc, value.data(), val_sz);
    }
    uint32_t checksum = crc ^ 0xFFFFFFFF;

    // Allocate buffer: 18 bytes header + val_sz bytes
    std::vector<uint8_t> buffer(RECORD_HEADER_SIZE + val_sz);
    buffer[0] = RECORD_MAGIC;
    buffer[1] = op_byte;
    encode_uint32_le(&buffer[2], checksum);
    std::memcpy(&buffer[6], key_buf, 8);
    std::memcpy(&buffer[14], sz_buf, 4);

    if (val_sz > 0) {
        std::memcpy(&buffer[RECORD_HEADER_SIZE], value.data(), val_sz);
    }

    return buffer;
}

} // namespace storage

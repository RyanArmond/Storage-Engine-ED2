#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace storage {

enum class OpType : uint8_t {
    PUT = 0x01,
    DELETE = 0x02
};

struct RecordHeader {
    uint8_t magic;       // 0xED
    uint8_t op_type;     // 0x01 (PUT) or 0x02 (DELETE)
    uint32_t checksum;   // CRC32 of op_type, key, val_size, and payload
    uint64_t key;        // 64-bit unsigned key
    uint32_t val_size;   // Size of payload in bytes (0 for DELETE)
};

constexpr uint8_t RECORD_MAGIC = 0xED;
constexpr size_t RECORD_HEADER_SIZE = 18; // 1 + 1 + 4 + 8 + 4

struct Record {
    OpType type;
    uint64_t key;
    std::string value;

    // Serializes record to raw binary buffer ready for append
    std::vector<uint8_t> serialize() const;
};

// Endianness helper utilities
void encode_uint32_le(uint8_t* dst, uint32_t val);
uint32_t decode_uint32_le(const uint8_t* src);
void encode_uint64_le(uint8_t* dst, uint64_t val);
uint64_t decode_uint64_le(const uint8_t* src);

} // namespace storage

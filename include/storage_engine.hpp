#pragma once

#include "record.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace storage {

struct KeyDirEntry {
    uint64_t record_offset; // Starting byte offset of the 18-byte record header
    uint64_t val_offset;    // Starting byte offset of value payload
    uint32_t val_size;      // Size of value in bytes
};

struct VerifyReport {
    bool is_healthy{true};
    uint64_t total_log_bytes{0};
    uint64_t total_records{0};
    uint64_t put_records{0};
    uint64_t delete_records{0};
    uint64_t active_keys{0};
    uint64_t corrupted_records{0};
    uint64_t active_bytes{0};
    std::vector<std::string> errors;
};

class StorageEngine {
public:
    StorageEngine();
    ~StorageEngine();

    // Prevent copy
    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    // Initializes storage layout in data_dir (creates directory & log file)
    static void init(const std::string& data_dir);

    // Verifies data directory integrity without mutating it
    static VerifyReport verify(const std::string& data_dir);

    // Opens engine and recovers in-memory index from append-only log
    void open(const std::string& data_dir);

    // Closes engine, syncing remaining buffers
    void close();

    // KV Operations
    bool put(uint64_t key, const std::string& value);
    bool get(uint64_t key, std::string& value);
    bool del(uint64_t key);

    // Inspection
    size_t active_key_count() const { return keydir_.size(); }
    uint64_t current_log_size() const { return append_offset_; }
    const std::string& data_dir() const { return data_dir_; }

private:
    void recover();
    static void pwrite_all(int fd, const void* buf, size_t count, off_t offset);
    static void pread_all(int fd, void* buf, size_t count, off_t offset);

    std::string data_dir_;
    std::string log_file_path_;
    int log_fd_{-1};
    uint64_t append_offset_{0};

    // In-memory index: KeyDir (Key -> KeyDirEntry)
    std::unordered_map<uint64_t, KeyDirEntry> keydir_;
};

} // namespace storage

#include "storage_engine.hpp"
#include "checksum.hpp"
#include "record.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace storage {

namespace fs = std::filesystem;

namespace {

void safe_truncate(int fd, off_t length) {
    if (::ftruncate(fd, length) < 0) {
        // If truncation fails, it could be due to permission or filesystem issues
        // We still break to prevent further corruption
    }
}

} // namespace

StorageEngine::StorageEngine() = default;

StorageEngine::~StorageEngine() {
    close();
}

void StorageEngine::init(const std::string& data_dir) {
    std::error_code ec;
    fs::create_directories(data_dir, ec);
    if (ec) {
        throw std::runtime_error("Failed to create data directory: " + ec.message());
    }

    std::string log_path = (fs::path(data_dir) / "data.log").string();
    int fd = ::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to initialize log file: " + std::string(std::strerror(errno)));
    }
    ::close(fd);
}

void StorageEngine::open(const std::string& data_dir) {
    close();

    if (!fs::exists(data_dir)) {
        throw std::runtime_error("Data directory does not exist: " + data_dir + " (run 'init' first)");
    }

    data_dir_ = data_dir;
    log_file_path_ = (fs::path(data_dir) / "data.log").string();

    log_fd_ = ::open(log_file_path_.c_str(), O_RDWR | O_CREAT, 0644);
    if (log_fd_ < 0) {
        throw std::runtime_error("Failed to open log file: " + std::string(std::strerror(errno)));
    }

    // Replay log and rebuild index
    recover();
}

void StorageEngine::close() {
    if (log_fd_ >= 0) {
        ::fdatasync(log_fd_);
        ::close(log_fd_);
        log_fd_ = -1;
    }
    keydir_.clear();
    append_offset_ = 0;
}

void StorageEngine::pwrite_all(int fd, const void* buf, size_t count, off_t offset) {
    const auto* ptr = static_cast<const uint8_t*>(buf);
    size_t total_written = 0;
    while (total_written < count) {
        ssize_t written = ::pwrite(fd, ptr + total_written, count - total_written, offset + total_written);
        if (written < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("Disk write error (pwrite): " + std::string(std::strerror(errno)));
        }
        if (written == 0) {
            throw std::runtime_error("Disk write returned 0 bytes (disk might be full)");
        }
        total_written += written;
    }
}

void StorageEngine::pread_all(int fd, void* buf, size_t count, off_t offset) {
    auto* ptr = static_cast<uint8_t*>(buf);
    size_t total_read = 0;
    while (total_read < count) {
        ssize_t n = ::pread(fd, ptr + total_read, count - total_read, offset + total_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("Disk read error (pread): " + std::string(std::strerror(errno)));
        }
        if (n == 0) {
            throw std::runtime_error("Disk read error: unexpected EOF at offset " + std::to_string(offset + total_read));
        }
        total_read += n;
    }
}

void StorageEngine::recover() {
    keydir_.clear();
    append_offset_ = 0;

    struct stat st{};
    if (::fstat(log_fd_, &st) < 0) {
        throw std::runtime_error("Failed to stat log file: " + std::string(std::strerror(errno)));
    }
    uint64_t file_size = static_cast<uint64_t>(st.st_size);
    uint64_t current_offset = 0;

    std::vector<uint8_t> header_buf(RECORD_HEADER_SIZE);
    std::vector<uint8_t> payload_buf;

    while (current_offset < file_size) {
        // Check if there is enough space left for at least the header
        if (current_offset + RECORD_HEADER_SIZE > file_size) {
            // Torn write at EOF: header truncated. Truncate file and stop.
            safe_truncate(log_fd_, current_offset);
            break;
        }

        // Read header
        ssize_t r = ::pread(log_fd_, header_buf.data(), RECORD_HEADER_SIZE, current_offset);
        if (r < static_cast<ssize_t>(RECORD_HEADER_SIZE)) {
            safe_truncate(log_fd_, current_offset);
            break;
        }

        uint8_t magic = header_buf[0];
        if (magic != RECORD_MAGIC) {
            // Corrupted record / torn write detected at current_offset
            safe_truncate(log_fd_, current_offset);
            break;
        }

        uint8_t op_type = header_buf[1];
        if (op_type != static_cast<uint8_t>(OpType::PUT) && op_type != static_cast<uint8_t>(OpType::DELETE)) {
            safe_truncate(log_fd_, current_offset);
            break;
        }

        uint32_t stored_crc = decode_uint32_le(&header_buf[2]);
        uint64_t key = decode_uint64_le(&header_buf[6]);
        uint32_t val_size = decode_uint32_le(&header_buf[14]);

        if (op_type == static_cast<uint8_t>(OpType::DELETE) && val_size != 0) {
            safe_truncate(log_fd_, current_offset);
            break;
        }

        // Check if full payload exists in file
        if (current_offset + RECORD_HEADER_SIZE + val_size > file_size) {
            // Torn write: payload truncated
            safe_truncate(log_fd_, current_offset);
            break;
        }

        // Read payload if any
        if (val_size > payload_buf.size()) {
            payload_buf.resize(val_size);
        }
        if (val_size > 0) {
            ssize_t pr = ::pread(log_fd_, payload_buf.data(), val_size, current_offset + RECORD_HEADER_SIZE);
            if (pr < static_cast<ssize_t>(val_size)) {
                safe_truncate(log_fd_, current_offset);
                break;
            }
        }

        // Verify CRC32
        uint32_t calc_crc = 0xFFFFFFFF;
        calc_crc = CRC32::update(calc_crc, &op_type, 1);
        calc_crc = CRC32::update(calc_crc, &header_buf[6], 8);  // key
        calc_crc = CRC32::update(calc_crc, &header_buf[14], 4); // val_size
        if (val_size > 0) {
            calc_crc = CRC32::update(calc_crc, payload_buf.data(), val_size);
        }
        calc_crc ^= 0xFFFFFFFF;

        if (calc_crc != stored_crc) {
            // CRC mismatch: data corruption (torn write). Truncate to last valid record.
            safe_truncate(log_fd_, current_offset);
            break;
        }

        // Valid record: update in-memory index
        if (op_type == static_cast<uint8_t>(OpType::PUT)) {
            keydir_[key] = KeyDirEntry{
                .record_offset = current_offset,
                .val_offset = current_offset + RECORD_HEADER_SIZE,
                .val_size = val_size
            };
        } else if (op_type == static_cast<uint8_t>(OpType::DELETE)) {
            keydir_.erase(key);
        }

        current_offset += RECORD_HEADER_SIZE + val_size;
    }

    append_offset_ = current_offset;
}

bool StorageEngine::put(uint64_t key, const std::string& value) {
    Record rec{
        .type = OpType::PUT,
        .key = key,
        .value = value
    };

    std::vector<uint8_t> buffer = rec.serialize();
    pwrite_all(log_fd_, buffer.data(), buffer.size(), append_offset_);
    ::fdatasync(log_fd_);

    keydir_[key] = KeyDirEntry{
        .record_offset = append_offset_,
        .val_offset = append_offset_ + RECORD_HEADER_SIZE,
        .val_size = static_cast<uint32_t>(value.size())
    };

    append_offset_ += buffer.size();
    return true;
}

bool StorageEngine::get(uint64_t key, std::string& value) {
    auto it = keydir_.find(key);
    if (it == keydir_.end()) {
        return false;
    }

    const KeyDirEntry& entry = it->second;
    value.resize(entry.val_size);
    if (entry.val_size > 0) {
        pread_all(log_fd_, value.data(), entry.val_size, entry.val_offset);
    }
    return true;
}

bool StorageEngine::del(uint64_t key) {
    auto it = keydir_.find(key);
    if (it == keydir_.end()) {
        return false; // Key not found
    }

    Record rec{
        .type = OpType::DELETE,
        .key = key,
        .value = ""
    };

    std::vector<uint8_t> buffer = rec.serialize();
    pwrite_all(log_fd_, buffer.data(), buffer.size(), append_offset_);
    ::fdatasync(log_fd_);

    keydir_.erase(it);
    append_offset_ += buffer.size();
    return true;
}

VerifyReport StorageEngine::verify(const std::string& data_dir) {
    VerifyReport report;

    if (!fs::exists(data_dir)) {
        report.is_healthy = false;
        report.errors.push_back("Data directory does not exist: " + data_dir);
        return report;
    }

    std::string log_path = (fs::path(data_dir) / "data.log").string();
    if (!fs::exists(log_path)) {
        report.is_healthy = false;
        report.errors.push_back("Log file data.log not found in: " + data_dir);
        return report;
    }

    int fd = ::open(log_path.c_str(), O_RDONLY);
    if (fd < 0) {
        report.is_healthy = false;
        report.errors.push_back("Unable to open log file for verification: " + std::string(std::strerror(errno)));
        return report;
    }

    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        ::close(fd);
        report.is_healthy = false;
        report.errors.push_back("Failed to stat log file: " + std::string(std::strerror(errno)));
        return report;
    }

    report.total_log_bytes = static_cast<uint64_t>(st.st_size);
    uint64_t current_offset = 0;
    std::unordered_map<uint64_t, KeyDirEntry> test_keydir;

    std::vector<uint8_t> header_buf(RECORD_HEADER_SIZE);
    std::vector<uint8_t> payload_buf;

    while (current_offset < report.total_log_bytes) {
        if (current_offset + RECORD_HEADER_SIZE > report.total_log_bytes) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("Truncated record header at offset " + std::to_string(current_offset));
            break;
        }

        ssize_t r = ::pread(fd, header_buf.data(), RECORD_HEADER_SIZE, current_offset);
        if (r < static_cast<ssize_t>(RECORD_HEADER_SIZE)) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("Incomplete header read at offset " + std::to_string(current_offset));
            break;
        }

        uint8_t magic = header_buf[0];
        if (magic != RECORD_MAGIC) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("Invalid magic byte (0x" + std::to_string(magic) + ") at offset " + std::to_string(current_offset));
            break;
        }

        uint8_t op_type = header_buf[1];
        if (op_type != static_cast<uint8_t>(OpType::PUT) && op_type != static_cast<uint8_t>(OpType::DELETE)) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("Invalid operation type (" + std::to_string(op_type) + ") at offset " + std::to_string(current_offset));
            break;
        }

        uint32_t stored_crc = decode_uint32_le(&header_buf[2]);
        uint64_t key = decode_uint64_le(&header_buf[6]);
        uint32_t val_size = decode_uint32_le(&header_buf[14]);

        if (op_type == static_cast<uint8_t>(OpType::DELETE) && val_size != 0) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("DELETE record with non-zero val_size at offset " + std::to_string(current_offset));
            break;
        }

        if (current_offset + RECORD_HEADER_SIZE + val_size > report.total_log_bytes) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("Truncated payload for key " + std::to_string(key) + " at offset " + std::to_string(current_offset));
            break;
        }

        if (val_size > payload_buf.size()) {
            payload_buf.resize(val_size);
        }
        if (val_size > 0) {
            ssize_t pr = ::pread(fd, payload_buf.data(), val_size, current_offset + RECORD_HEADER_SIZE);
            if (pr < static_cast<ssize_t>(val_size)) {
                report.is_healthy = false;
                report.corrupted_records++;
                report.errors.push_back("Failed to read payload at offset " + std::to_string(current_offset + RECORD_HEADER_SIZE));
                break;
            }
        }

        uint32_t calc_crc = 0xFFFFFFFF;
        calc_crc = CRC32::update(calc_crc, &op_type, 1);
        calc_crc = CRC32::update(calc_crc, &header_buf[6], 8);
        calc_crc = CRC32::update(calc_crc, &header_buf[14], 4);
        if (val_size > 0) {
            calc_crc = CRC32::update(calc_crc, payload_buf.data(), val_size);
        }
        calc_crc ^= 0xFFFFFFFF;

        if (calc_crc != stored_crc) {
            report.is_healthy = false;
            report.corrupted_records++;
            report.errors.push_back("CRC32 mismatch at offset " + std::to_string(current_offset));
            break;
        }

        report.total_records++;
        if (op_type == static_cast<uint8_t>(OpType::PUT)) {
            report.put_records++;
            test_keydir[key] = KeyDirEntry{
                .record_offset = current_offset,
                .val_offset = current_offset + RECORD_HEADER_SIZE,
                .val_size = val_size
            };
        } else {
            report.delete_records++;
            test_keydir.erase(key);
        }

        current_offset += RECORD_HEADER_SIZE + val_size;
    }

    report.active_keys = test_keydir.size();
    for (const auto& [_, entry] : test_keydir) {
        report.active_bytes += entry.val_size;
    }

    ::close(fd);
    return report;
}

} // namespace storage

#include "cli.hpp"
#include "json.hpp"
#include "storage_engine.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace storage {

void CLI::print_usage(const char* prog_name) {
    std::cout << "Usage:\n"
              << "  " << prog_name << " init --data-dir <path>\n"
              << "  " << prog_name << " run --data-dir <path> --input <file.jsonl> --output <file.jsonl>\n"
              << "  " << prog_name << " verify --data-dir <path>\n"
              << "  " << prog_name << " describe\n\n"
              << "Commands:\n"
              << "  init      Initialize storage structures in specified directory\n"
              << "  run       Execute workload from input JSONL and write output JSONL\n"
              << "  verify    Verify log consistency and structural integrity\n"
              << "  describe  Display storage engine architectural details\n";
}

int CLI::handle_init(const std::string& data_dir) {
    if (data_dir.empty()) {
        std::cerr << "Error: --data-dir is required for 'init'\n";
        return 1;
    }

    try {
        StorageEngine::init(data_dir);
        std::cout << "Storage engine initialized successfully in: " << data_dir << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Initialization error: " << e.what() << "\n";
        return 1;
    }
}

int CLI::handle_run(const std::string& data_dir, const std::string& input_file, const std::string& output_file) {
    if (data_dir.empty()) {
        std::cerr << "Error: --data-dir is required for 'run'\n";
        return 1;
    }
    if (input_file.empty()) {
        std::cerr << "Error: --input is required for 'run'\n";
        return 1;
    }
    if (output_file.empty()) {
        std::cerr << "Error: --output is required for 'run'\n";
        return 1;
    }

    std::ifstream in(input_file);
    if (!in.is_open()) {
        std::cerr << "Error: Could not open input file: " << input_file << "\n";
        return 1;
    }

    std::ofstream out(output_file, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "Error: Could not open output file: " << output_file << "\n";
        return 1;
    }

    try {
        StorageEngine engine;
        engine.open(data_dir);

        std::string line;
        uint64_t processed_ops = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (std::getline(in, line)) {
            // Trim leading/trailing whitespace
            size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue; // Skip blank line

            WorkloadCommand cmd;
            if (!parse_workload_command(line, cmd)) {
                std::cerr << "Warning: Skipping malformed JSON line: " << line << "\n";
                continue;
            }

            if (cmd.op == "put") {
                engine.put(cmd.key, cmd.value);
                out << format_response(cmd.id, "ok") << "\n";
            } else if (cmd.op == "get") {
                std::string val;
                if (engine.get(cmd.key, val)) {
                    out << format_response(cmd.id, "ok", val) << "\n";
                } else {
                    out << format_response(cmd.id, "not_found") << "\n";
                }
            } else if (cmd.op == "delete" || cmd.op == "del") {
                if (engine.del(cmd.key)) {
                    out << format_response(cmd.id, "ok") << "\n";
                } else {
                    out << format_response(cmd.id, "not_found") << "\n";
                }
            } else {
                std::cerr << "Warning: Unknown operation: " << cmd.op << "\n";
                out << format_response(cmd.id, "error") << "\n";
            }
            processed_ops++;
        }

        out.flush();
        engine.close();

        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

        std::cout << "Executed " << processed_ops << " operations in " 
                  << std::fixed << std::setprecision(2) << elapsed.count() << " ms.\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Execution error: " << e.what() << "\n";
        return 1;
    }
}

int CLI::handle_verify(const std::string& data_dir) {
    if (data_dir.empty()) {
        std::cerr << "Error: --data-dir is required for 'verify'\n";
        return 1;
    }

    VerifyReport rep = StorageEngine::verify(data_dir);

    std::cout << "========================================\n"
              << "       STORAGE INTEGRITY REPORT         \n"
              << "========================================\n"
              << "Status:            " << (rep.is_healthy ? "HEALTHY (OK)" : "CORRUPTED / WARNINGS") << "\n"
              << "Log File Size:     " << rep.total_log_bytes << " bytes\n"
              << "Total Records:     " << rep.total_records << "\n"
              << "  - PUT Records:   " << rep.put_records << "\n"
              << "  - DEL Records:   " << rep.delete_records << "\n"
              << "Active Keys in RAM:" << rep.active_keys << "\n"
              << "Active Value Bytes:" << rep.active_bytes << " bytes\n"
              << "Corrupted Records: " << rep.corrupted_records << "\n";

    if (!rep.errors.empty()) {
        std::cout << "\nErrors / Anomalies Encountered:\n";
        for (const auto& err : rep.errors) {
            std::cout << "  [!] " << err << "\n";
        }
    }
    std::cout << "========================================\n";

    return rep.is_healthy ? 0 : 1;
}

int CLI::handle_describe() {
    std::cout << "======================================================================\n"
              << "    ENTREGA 1: PERSISTENT STORAGE ENGINE (ED2) - ARCHITECTURE         \n"
              << "======================================================================\n\n"
              << "1. OVERVIEW & DESIGN PHILOSOPHY\n"
              << "   - Architecture: Bitcask-style Append-Only Log with In-Memory Index (KeyDir).\n"
              << "   - Primary Goals: O(1) random reads, high-throughput sequential appends,\n"
              << "     crash resilience with torn-write auto-recovery, and minimal complexity.\n\n"
              << "2. DISK STORAGE FORMAT (data.log)\n"
              << "   All operations (PUT and DELETE tombstones) are appended to a binary log.\n"
              << "   Each record contains an 18-byte fixed header followed by variable payload:\n\n"
              << "   +-------------+--------------+------------------+------------------+--------------------+-------------------------+\n"
              << "   | Magic (1B)  | OpType (1B)  | CRC32 (4B)       | Key (8B)         | ValSize (4B)       | Payload (ValSize bytes) |\n"
              << "   | 0xED        | 0x01: PUT    | IEEE 802.3 LE    | uint64_t LE      | uint32_t LE        | Raw bytes               |\n"
              << "   |             | 0x02: DELETE |                  |                  | (0 for DELETE)     |                         |\n"
              << "   +-------------+--------------+------------------+------------------+--------------------+-------------------------+\n\n"
              << "3. IN-MEMORY INDEX (KeyDir)\n"
              << "   - Structure: std::unordered_map<uint64_t, KeyDirEntry>\n"
              << "   - KeyDirEntry:\n"
              << "       * record_offset (uint64_t): Byte position of the 18-byte record header\n"
              << "       * val_offset    (uint64_t): Byte position of the value payload\n"
              << "       * val_size      (uint32_t): Size of the value in bytes\n"
              << "   - Complexity:\n"
              << "       * PUT:    O(1) amortized (append to log + update map)\n"
              << "       * GET:    O(1) (map lookup + single positional pread on disk)\n"
              << "       * DELETE: O(1) amortized (append tombstone + map erase)\n\n"
              << "4. CRASH RECOVERY & DATA INTEGRITY\n"
              << "   - Recovery on Startup: Replays data.log sequentially from offset 0.\n"
              << "   - Torn Write Protection: If a system crash occurs mid-write, partial records\n"
              << "     or CRC32 mismatches at EOF are automatically detected. The file is\n"
              << "     safely truncated to the last known valid record offset.\n"
              << "   - Durability: Synchronous fdatasync() ensures persistent state on disk.\n\n"
              << "5. PROTOCOL & CLI\n"
              << "   - JSON Lines protocol support with stream parsing and RFC-compliant escaping.\n"
              << "   - Commands: init, run, verify, describe.\n"
              << "======================================================================\n";
    return 0;
}

int CLI::execute(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    if (command == "--help" || command == "-h") {
        print_usage(argv[0]);
        return 0;
    }

    std::string data_dir;
    std::string input_file;
    std::string output_file;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data-dir" && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (arg == "--input" && i + 1 < argc) {
            input_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (command == "init") {
        return handle_init(data_dir);
    } else if (command == "run") {
        return handle_run(data_dir, input_file, output_file);
    } else if (command == "verify") {
        return handle_verify(data_dir);
    } else if (command == "describe") {
        return handle_describe();
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        print_usage(argv[0]);
        return 1;
    }
}

} // namespace storage

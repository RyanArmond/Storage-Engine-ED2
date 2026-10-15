#pragma once

#include <string>
#include <vector>

namespace storage {

class CLI {
public:
    static int execute(int argc, char* argv[]);

private:
    static void print_usage(const char* prog_name);
    static int handle_init(const std::string& data_dir);
    static int handle_run(const std::string& data_dir, const std::string& input_file, const std::string& output_file);
    static int handle_verify(const std::string& data_dir);
    static int handle_describe();
};

} // namespace storage

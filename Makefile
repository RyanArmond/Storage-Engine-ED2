CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude
LDFLAGS ?= 

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
TARGET = engine

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all clean test

all: $(TARGET) storage_engine

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

storage_engine: $(TARGET)
	ln -sf $(TARGET) storage_engine

test: all
	@bash tests/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET) storage_engine

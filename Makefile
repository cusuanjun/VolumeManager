# Makefile for VolumeManager

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread
INCLUDE_DIRS = -I./include
SRC_DIR = ./src
TEST_DIR = ./test
BUILD_DIR = ./build

# 创建构建目录
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 编译单元测试程序
test_unit: $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -o $(BUILD_DIR)/test_volume_manager \
		$(TEST_DIR)/test_volume_manager.cpp \
		$(SRC_DIR)/volume_manager.cpp \
		$(SRC_DIR)/serializer.cpp \
		$(SRC_DIR)/compression_utils.cpp \
		$(SRC_DIR)/async_file_io.cpp \
		$(SRC_DIR)/error_codes.cpp \
		$(SRC_DIR)/utils.cpp \
		-lz
	@echo "编译完成，运行单元测试..."
	@$(BUILD_DIR)/test_volume_manager

# 编译端到端测试程序
test_e2e: $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE_DIRS) -o $(BUILD_DIR)/test_e2e \
		$(TEST_DIR)/e2e_test.cpp \
		$(SRC_DIR)/volume_manager.cpp \
		$(SRC_DIR)/serializer.cpp \
		$(SRC_DIR)/compression_utils.cpp \
		$(SRC_DIR)/async_file_io.cpp \
		$(SRC_DIR)/error_codes.cpp \
		$(SRC_DIR)/utils.cpp \
		-lz
	@echo "编译完成，运行端到端测试..."
	@$(BUILD_DIR)/test_e2e

# 编译所有测试
test: test_unit test_e2e

# 清理构建文件
clean:
	rm -rf $(BUILD_DIR)

.PHONY: test test_unit test_e2e clean

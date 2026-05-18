BUILD_DIR := build
DEBUG_BUILD_DIR := build-debug

.PHONY: build build-debug run run-debug clean

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo
	@cmake --build $(BUILD_DIR)
	@ln -sf $(BUILD_DIR)/compile_commands.json compile_commands.json

build-debug:
	@cmake -S . -B $(DEBUG_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DTWAIN_DEBUG=ON
	@cmake --build $(DEBUG_BUILD_DIR)

run: build
	@./$(BUILD_DIR)/twain

run-debug: build-debug
	@./$(DEBUG_BUILD_DIR)/twain-debug

clean:
	@rm -rf $(BUILD_DIR) $(DEBUG_BUILD_DIR)

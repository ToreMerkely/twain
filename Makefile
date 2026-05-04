BUILD_DIR := build

.PHONY: build run clean

build:
	@cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	@cmake --build $(BUILD_DIR)
	@ln -sf $(BUILD_DIR)/compile_commands.json compile_commands.json

run: build
	@./$(BUILD_DIR)/twain

clean:
	@rm -rf $(BUILD_DIR)

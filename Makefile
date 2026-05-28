BUILD_DIR := build
DEBUG_BUILD_DIR := build-debug
TEST_BUILD_DIR := build-test

.PHONY: build build-debug run run-debug test coverage clean

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

test:
	@cmake -S . -B $(TEST_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DTWAIN_TESTS=ON
	@cmake --build $(TEST_BUILD_DIR) --target twain_tests
	@./$(TEST_BUILD_DIR)/twain_tests

coverage:
	@command -v lcov >/dev/null || { echo "lcov is required: sudo apt install lcov"; exit 1; }
	@cmake -S . -B $(TEST_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DTWAIN_TESTS=ON -DTWAIN_COVERAGE=ON
	@cmake --build $(TEST_BUILD_DIR) --target twain_tests
	@find $(TEST_BUILD_DIR) -name '*.gcda' -delete
	@./$(TEST_BUILD_DIR)/twain_tests >/dev/null
	@lcov --quiet --capture --directory $(TEST_BUILD_DIR) --output-file $(TEST_BUILD_DIR)/coverage.info --ignore-errors mismatch,inconsistent,gcov
	@lcov --quiet --remove $(TEST_BUILD_DIR)/coverage.info '/usr/*' '*/Qt*' '*/tests/*' '*/build-test/*' --output-file $(TEST_BUILD_DIR)/coverage.info --ignore-errors unused
	@lcov --summary $(TEST_BUILD_DIR)/coverage.info
	@genhtml --quiet $(TEST_BUILD_DIR)/coverage.info --output-directory $(TEST_BUILD_DIR)/coverage --ignore-errors inconsistent
	@echo "HTML report: file://$(CURDIR)/$(TEST_BUILD_DIR)/coverage/index.html"

clean:
	@rm -rf $(BUILD_DIR) $(DEBUG_BUILD_DIR) $(TEST_BUILD_DIR)

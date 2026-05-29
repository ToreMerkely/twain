BUILD_DIR := build
DEBUG_BUILD_DIR := build-debug
TEST_BUILD_DIR := build-test

.PHONY: build build-debug run run-debug test clean

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
	@command -v lcov >/dev/null || { echo "lcov is required: sudo apt install lcov"; exit 1; }
	@cmake -S . -B $(TEST_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug -DTWAIN_TESTS=ON -DTWAIN_COVERAGE=ON
	@cmake --build $(TEST_BUILD_DIR)
	@find $(TEST_BUILD_DIR) -name '*.gcda' -delete
	@lcov --quiet --initial --capture --directory $(TEST_BUILD_DIR) --output-file $(TEST_BUILD_DIR)/baseline.info --ignore-errors mismatch,inconsistent,gcov
	@ctest --test-dir $(TEST_BUILD_DIR) --output-on-failure
	@lcov --quiet --capture --directory $(TEST_BUILD_DIR) --output-file $(TEST_BUILD_DIR)/tests.info --ignore-errors mismatch,inconsistent,gcov,empty
	@lcov --quiet --add-tracefile $(TEST_BUILD_DIR)/baseline.info --add-tracefile $(TEST_BUILD_DIR)/tests.info --output-file $(TEST_BUILD_DIR)/coverage.info --ignore-errors inconsistent
	@lcov --quiet --remove $(TEST_BUILD_DIR)/coverage.info '/usr/*' '*/tests/*' '*/build-test/*' --output-file $(TEST_BUILD_DIR)/coverage.info --ignore-errors unused
	@lcov --summary $(TEST_BUILD_DIR)/coverage.info
	@genhtml --quiet $(TEST_BUILD_DIR)/coverage.info --output-directory $(TEST_BUILD_DIR)/coverage --ignore-errors inconsistent
	@echo "HTML report: file://$(CURDIR)/$(TEST_BUILD_DIR)/coverage/index.html"

clean:
	@rm -rf $(BUILD_DIR) $(DEBUG_BUILD_DIR) $(TEST_BUILD_DIR)

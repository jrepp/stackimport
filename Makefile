BUILD_DIR ?= build

.PHONY: configure stackimport import-stacks test clean

configure:
	cmake -S . -B $(BUILD_DIR)

stackimport: configure
	cmake --build $(BUILD_DIR) --target stackimport

import-stacks: configure
	cmake --build $(BUILD_DIR) --target import-stacks

test: configure
	cmake --build $(BUILD_DIR) --target stackimport-tests
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	rm -rf $(BUILD_DIR)

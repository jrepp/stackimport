CXX ?= c++
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra
BUILD_DIR ?= build
STACKIMPORT_BINARY := $(BUILD_DIR)/stackimport
TEST_BINARY := $(BUILD_DIR)/stackimport-tests

.PHONY: stackimport import-stacks test clean

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(STACKIMPORT_BINARY): main.cpp woba.cpp picture.cpp CStackFile.cpp CBuf.cpp byteutils.cpp CStackFile.h CBuf.h woba.h picture.h byteutils.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DMAC_CODE=0 main.cpp woba.cpp picture.cpp CStackFile.cpp CBuf.cpp byteutils.cpp -o $(STACKIMPORT_BINARY)

stackimport: $(STACKIMPORT_BINARY)

import-stacks: $(STACKIMPORT_BINARY)
	scripts/import_all_stacks.py --nostatus --noprogress

$(TEST_BINARY): TestsMain.cpp Tests.cpp CStackFile.h CBuf.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) TestsMain.cpp Tests.cpp -o $(TEST_BINARY)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

clean:
	rm -rf $(BUILD_DIR)

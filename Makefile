TARGET_ARCH ?= native

BUILD_DIR := build
LOCAL_BIN_DIR := $(BUILD_DIR)/local-bin
EXAMPLE_SRC := $(sort $(wildcard examples/*.c))
EXAMPLE_NAMES := $(patsubst examples/%.c,%,$(EXAMPLE_SRC))

COMMON_SRC := $(sort $(wildcard src/*.c) $(wildcard src/common/*.c))
COMMON_INC := include

CFLAGS_common := -O2 -Wall -Wextra -I$(COMMON_INC)
LDLIBS_common := -lm -lasound

ifeq ($(TARGET_ARCH),armhf)
CC := arm-linux-gnueabihf-gcc
STRIP := arm-linux-gnueabihf-strip
BIN_DIR := $(BUILD_DIR)/bin/armhf
else ifeq ($(TARGET_ARCH),aarch64)
CC := aarch64-linux-gnu-gcc
STRIP := aarch64-linux-gnu-strip
BIN_DIR := $(BUILD_DIR)/bin/aarch64
else
CC := cc
STRIP := strip
BIN_DIR := $(LOCAL_BIN_DIR)
endif

BINARIES := $(addprefix $(BIN_DIR)/,$(EXAMPLE_NAMES))

.PHONY: all clean examples manifest

all: examples manifest

examples: $(BINARIES)

$(BIN_DIR):
	mkdir -p $@

$(BIN_DIR)/%: examples/%.c $(COMMON_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS_common) -o $@ $^ $(LDLIBS_common)
	$(STRIP) --strip-unneeded $@ 2>/dev/null || true

manifest: examples | $(BIN_DIR)
	@printf '%s\n' $(EXAMPLE_NAMES) > $(BIN_DIR)/examples.manifest

clean:
	rm -rf $(BUILD_DIR)

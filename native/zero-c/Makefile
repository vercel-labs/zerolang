CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Os
ROOT := ../..
OUT ?= $(ROOT)/.zero/bin/zero
BUILD_STAMP := $(OUT).build
HOST_ID := $(shell uname -s 2>/dev/null || echo unknown)-$(shell uname -m 2>/dev/null || echo unknown)
BUILD_ID := $(HOST_ID)|$(CC)|$(CFLAGS)
SRC := $(wildcard src/*.c) $(wildcard src/crypto/*.c)

.PHONY: all clean install-local FORCE

all: $(OUT)

$(BUILD_STAMP): FORCE
	@mkdir -p $(dir $(OUT))
	@current="$$(cat "$(BUILD_STAMP)" 2>/dev/null || true)"; \
	if [ "$$current" != "$(BUILD_ID)" ]; then \
		printf '%s\n' "$(BUILD_ID)" > $(BUILD_STAMP); \
	fi

$(OUT): $(BUILD_STAMP) $(SRC) include/zero.h
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -Iinclude $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) $(BUILD_STAMP)

install-local: $(OUT)
	@printf 'installed %s\n' "$(OUT)"

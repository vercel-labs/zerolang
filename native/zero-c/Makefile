CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Os
ROOT := ../..
OUT := $(ROOT)/.zero/bin/zero
NODE_TS := node --experimental-strip-types --disable-warning=ExperimentalWarning
BUILD_STAMP := $(OUT).build
HOST_ID := $(shell uname -s 2>/dev/null || echo unknown)-$(shell uname -m 2>/dev/null || echo unknown)
BUILD_ID := $(HOST_ID)|$(CC)|$(CFLAGS)
SRC := $(wildcard src/*.c)
INCLUDES := $(wildcard include/*.h) $(wildcard src/*.h) $(wildcard src/*.inc)
EMBEDDED_SKILLS := src/embedded_skills.inc
EMBEDDED_STDLIB := src/embedded_stdlib.inc
SKILL_INPUTS := $(ROOT)/skills/zero/SKILL.md $(wildcard $(ROOT)/skill-data/*.md)
STDLIB_INPUTS := $(wildcard $(ROOT)/std/*.0)

.PHONY: all clean install-local FORCE

all: $(OUT)

$(BUILD_STAMP): FORCE
	@mkdir -p $(dir $(OUT))
	@current="$$(cat "$(BUILD_STAMP)" 2>/dev/null || true)"; \
	if [ "$$current" != "$(BUILD_ID)" ]; then \
		printf '%s\n' "$(BUILD_ID)" > $(BUILD_STAMP); \
	fi

$(EMBEDDED_SKILLS): $(ROOT)/scripts/embed-skill-data.mts $(SKILL_INPUTS)
	cd $(ROOT) && $(NODE_TS) scripts/embed-skill-data.mts

$(EMBEDDED_STDLIB): $(ROOT)/scripts/embed-stdlib-sources.mts $(STDLIB_INPUTS)
	cd $(ROOT) && $(NODE_TS) scripts/embed-stdlib-sources.mts

$(OUT): $(BUILD_STAMP) $(SRC) $(INCLUDES) $(EMBEDDED_SKILLS) $(EMBEDDED_STDLIB)
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) -Iinclude $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) $(BUILD_STAMP)

install-local: $(OUT)
	@printf 'installed %s\n' "$(OUT)"

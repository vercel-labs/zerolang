CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Os
ROOT := ../..
OUT := $(ROOT)/.zero/bin/zero
NODE_TS := node --experimental-strip-types --disable-warning=ExperimentalWarning
BUILD_STAMP := $(OUT).build
HOST_ID := $(shell uname -s 2>/dev/null || echo unknown)-$(shell uname -m 2>/dev/null || echo unknown)
# Stamp the short git commit into the binary so `zero --version --json` reports
# a real commit on from-source builds. Falls back to "unknown" when git is
# unavailable or this is not a checkout; `make` itself must never fail here.
ZERO_GIT_COMMIT := $(shell git -C $(ROOT) rev-parse --short HEAD 2>/dev/null || echo unknown)
COMMIT_FLAGS := -DZERO_GIT_COMMIT='"$(ZERO_GIT_COMMIT)"'
BUILD_ID := $(HOST_ID)|$(CC)|$(CFLAGS)|$(ZERO_GIT_COMMIT)
SRC := $(wildcard src/*.c)
INCLUDES := $(wildcard include/*.h) $(wildcard src/*.inc)
EMBEDDED_SKILLS := src/embedded_skills.inc
SKILL_INPUTS := $(ROOT)/skills/zero/SKILL.md $(wildcard $(ROOT)/skill-data/*.md)

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

$(OUT): $(BUILD_STAMP) $(SRC) $(INCLUDES) $(EMBEDDED_SKILLS)
	@mkdir -p $(dir $(OUT))
	$(CC) $(CFLAGS) $(COMMIT_FLAGS) -Iinclude $(SRC) -o $(OUT)

clean:
	rm -f $(OUT) $(BUILD_STAMP)

install-local: $(OUT)
	@printf 'installed %s\n' "$(OUT)"

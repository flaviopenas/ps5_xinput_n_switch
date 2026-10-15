# Optional developer formatting; third-party code and archived probes are excluded.
CLANG_FORMAT ?= $(or $(wildcard testing_tools/.tools/format/bin/clang-format),clang-format-18)
FORMAT_DIRS := src/app src/platform src/usb src/bluetooth
FORMAT_SOURCES := $(foreach dir,$(FORMAT_DIRS),$(wildcard $(dir)/*.c $(dir)/*.h))

.PHONY: format format-check
format:
	$(CLANG_FORMAT) --style=file:testing_tools/.clang-format -i $(FORMAT_SOURCES)

format-check:
	$(CLANG_FORMAT) --style=file:testing_tools/.clang-format --dry-run --Werror $(FORMAT_SOURCES)

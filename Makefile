.DEFAULT_GOAL := all
include mk/config.mk
include mk/manager.mk

.PHONY: all manager
all: manager
manager: dist/ps5-xinput-manager.elf
	sha256sum $< > dist/SHA256SUMS.manager
	python3 scripts/package_manager.py

build/release/%.o: %.c $(PROJECT_HEADERS)
	@mkdir -p build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@

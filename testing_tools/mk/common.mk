.PHONY: all release debug usb-probe usb-transport-probe usb-controller-test usb-controller-debug usb-in-probe bt-probe bt-radio-probe bt-scan bt-pair bt-pair-trial bt-capture bt-bridge manager host-test
all: host-test
release: testing_tools/dist/ps5-xinput.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS
debug: testing_tools/dist/ps5-xinput-debug.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.debug
usb-probe: testing_tools/dist/ps5-xinput-usb-probe.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.usb-probe

testing_tools/dist/ps5-xinput.elf: $(RELEASE_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-debug.elf: $(DEBUG_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/release/%.o: %.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@

testing_tools/build/debug/%.o: %.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -c $< -o $@


testing_tools/build/release/usb_bridge.o: testing_tools/src/usb/usb_bridge_legacy.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@
testing_tools/build/debug/usb_bridge.o: testing_tools/src/usb/usb_bridge_legacy.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -c $< -o $@
testing_tools/build/release/vpad.o: testing_tools/src/platform/vpad_variants.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@
testing_tools/build/debug/vpad.o: testing_tools/src/platform/vpad_variants.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -c $< -o $@

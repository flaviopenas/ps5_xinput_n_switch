usb-transport-probe: testing_tools/dist/ps5-xinput-usb-transport-probe.elf
testing_tools/dist/ps5-xinput-usb-transport-probe.elf: testing_tools/build/debug/usb_transport_probe.o testing_tools/build/debug/gip.o testing_tools/build/debug/switch_usb.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

usb-controller-test: testing_tools/dist/ps5-xinput-usb-controller-test.elf
testing_tools/dist/ps5-xinput-usb-controller-test.elf: testing_tools/build/release/main_usb_test.o $(filter-out testing_tools/build/release/usb_bridge.o,$(RELEASE_OBJS))
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
testing_tools/build/release/main_usb_test.o: testing_tools/src/usb/usb_bridge_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DXINPUT_USB_TEST_PORT=9025 -c $< -o $@

usb-controller-debug: testing_tools/dist/ps5-xinput-usb-controller-debug.elf
testing_tools/dist/ps5-xinput-usb-controller-debug.elf: testing_tools/build/debug/main_usb_test.o $(filter-out testing_tools/build/debug/usb_bridge.o,$(DEBUG_OBJS))
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
testing_tools/build/debug/main_usb_test.o: testing_tools/src/usb/usb_bridge_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DXINPUT_USB_TEST_PORT=9025 -c $< -o $@
testing_tools/dist/ps5-xinput-usb-probe.elf: testing_tools/build/debug/usb_probe.o testing_tools/build/debug/gip.o testing_tools/build/debug/switch_usb.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
usb-in-probe: testing_tools/dist/ps5-xinput-in-probe.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.in-probe
IN_PROBE_OBJS := $(filter-out testing_tools/build/debug/gip.o,$(DEBUG_OBJS)) testing_tools/build/debug/gip_in_probe.o
testing_tools/dist/ps5-xinput-in-probe.elf: $(IN_PROBE_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
testing_tools/build/debug/gip_in_probe.o: src/usb/gip.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DUSB_SWITCH_IN_ONLY_TEST -c $< -o $@

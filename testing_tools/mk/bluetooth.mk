bt-scan: testing_tools/dist/ps5-xinput-bt-scan.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-scan
bt-pair: testing_tools/dist/ps5-xinput-bt-pair.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-pair
bt-pair-trial: testing_tools/dist/ps5-xinput-bt-pair-trial.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-pair-trial
bt-capture: testing_tools/dist/ps5-xinput-bt-capture.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-capture
bt-bridge: testing_tools/dist/ps5-xinput-bt.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt
testing_tools/build/debug/bt_bridge.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HID_CAPTURE=1 -DBT_BRIDGE=1 -DBT_BOND_PATH='"/data/ps5-xinput/bt-bond-trial.bin"' -c $< -o $@

testing_tools/build/debug/bt_xbox.o: src/bluetooth/bt_xbox_map.inc

testing_tools/dist/ps5-xinput-bt.elf: testing_tools/build/debug/bt_bridge.o testing_tools/build/debug/bt_xbox.o testing_tools/build/debug/bt_timing.o testing_tools/build/debug/bt_hog.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o testing_tools/build/release/vpad.o testing_tools/build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/debug/bt_capture.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HID_CAPTURE=1 -DBT_BOND_PATH='"/data/ps5-xinput/bt-bond-trial.bin"' -c $< -o $@

testing_tools/dist/ps5-xinput-bt-capture.elf: testing_tools/build/debug/bt_capture.o testing_tools/build/debug/bt_hog.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/debug/bt_pair_trial.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_BOND_PATH='"/data/ps5-xinput/bt-bond-trial.bin"' -c $< -o $@

testing_tools/dist/ps5-xinput-bt-pair-trial.elf: testing_tools/build/debug/bt_pair_trial.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-bt-pair.elf: testing_tools/build/debug/bt_pair.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-bt-scan.elf: testing_tools/build/debug/bt_scan.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-bt-radio-probe.elf: testing_tools/build/debug/bt_radio_probe.o testing_tools/build/debug/bt_hci.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-bt-probe.elf: testing_tools/build/debug/bt_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^


# Legacy pairing-only executable shares the session source.
testing_tools/build/debug/bt_pair.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -c $< -o $@

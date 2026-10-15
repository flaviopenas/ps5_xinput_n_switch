bt-probe: testing_tools/dist/ps5-xinput-bt-probe.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-probe
bt-radio-probe: testing_tools/dist/ps5-xinput-bt-radio-probe.elf
	sha256sum $< > testing_tools/dist/SHA256SUMS.bt-radio-probe

.PHONY: bt-secondary-probe
.PHONY: bt-secondary-bulk-probe
.PHONY: bt-secondary-bridge
.PHONY: bt-secondary-cleanup
bt-secondary-cleanup: testing_tools/dist/ps5-xinput-secondary-cleanup.elf
testing_tools/build/debug/bt_hci_secondary_cleanup.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -DBT_HCI_DRAIN_CANCELLED=1 -c $< -o $@
testing_tools/build/debug/bt_cleanup_observer.o: testing_tools/src/diagnostics/bt_reconnect_observer.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_OBSERVER_ACL=1 -c $< -o $@
testing_tools/dist/ps5-xinput-secondary-cleanup.elf: testing_tools/build/debug/bt_cleanup_observer.o testing_tools/build/debug/bt_hci_secondary_cleanup.o testing_tools/build/debug/bt_cancel_drain.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-secondary-bridge: testing_tools/dist/ps5-xinput-bt-secondary.elf
.PHONY: bt-secondary-minimal
bt-secondary-minimal: testing_tools/dist/ps5-xinput-bt-secondary-minimal.elf
testing_tools/build/debug/bt_hci_secondary_minimal.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -DBT_HCI_BULK_COMMAND=1 -DBT_HCI_EVENT_READS=1 -DBT_HCI_ACL_READS=1 -DBT_HCI_DRAIN_CANCELLED=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-secondary-minimal.elf: testing_tools/build/debug/bt_bridge_secondary.o testing_tools/build/debug/bt_xbox.o testing_tools/build/debug/bt_timing.o testing_tools/build/debug/bt_hog.o testing_tools/build/debug/bt_hci_secondary_minimal.o testing_tools/build/debug/bt_cancel_drain.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o testing_tools/build/release/vpad.o testing_tools/build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
testing_tools/build/debug/bt_hci_secondary_bridge.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -DBT_HCI_BULK_COMMAND=1 -DBT_HCI_ACL_READS=64 -c $< -o $@
testing_tools/build/debug/bt_bridge_secondary.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HID_CAPTURE=1 -DBT_BRIDGE=1 -DBT_BOND_PATH='"/data/ps5-xinput/bt-bond-secondary.bin"' -c $< -o $@
testing_tools/dist/ps5-xinput-bt-secondary.elf: testing_tools/build/debug/bt_bridge_secondary.o testing_tools/build/debug/bt_xbox.o testing_tools/build/debug/bt_timing.o testing_tools/build/debug/bt_hog.o testing_tools/build/debug/bt_hci_secondary_bridge.o testing_tools/build/debug/bt_advert.o testing_tools/build/debug/log.o testing_tools/build/release/vpad.o testing_tools/build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-secondary-bulk-probe: testing_tools/dist/ps5-xinput-bt-secondary-bulk-probe.elf
testing_tools/build/debug/bt_hci_secondary_bulk.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -DBT_HCI_EVENT_READS=1 -DBT_HCI_BULK_COMMAND_PROBE=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-secondary-bulk-probe.elf: testing_tools/build/debug/bt_radio_probe.o testing_tools/build/debug/bt_hci_secondary_bulk.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
.PHONY: bt-reconnect-observer
.PHONY: bt-secondary-observer
bt-secondary-observer: testing_tools/dist/ps5-xinput-secondary-observer.elf
testing_tools/build/debug/bt_hci_secondary_event.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -DBT_HCI_EVENT_READS=1 -c $< -o $@
testing_tools/dist/ps5-xinput-secondary-observer.elf: testing_tools/build/debug/bt_reconnect_observer.o testing_tools/build/debug/bt_hci_secondary_event.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-reconnect-observer: testing_tools/dist/ps5-xinput-reconnect-observer.elf
testing_tools/build/debug/bt_hci_single_event.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_EVENT_READS=1 -c $< -o $@
testing_tools/dist/ps5-xinput-reconnect-observer.elf: testing_tools/build/debug/bt_reconnect_observer.o testing_tools/build/debug/bt_hci_single_event.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
.PHONY: bt-native-probe
.PHONY: bt-library-probe
.PHONY: bt-load-probe
.PHONY: bt-inventory
.PHONY: bt-client-probe
.PHONY: bt-native-query
.PHONY: bt-native-events
.PHONY: bt-native-event-discovery
.PHONY: bt-native-event-trace
.PHONY: bt-native-connect-trial
.PHONY: bt-native-pair-trial
bt-native-pair-trial: testing_tools/dist/ps5-xinput-bt-native-pair-trial.elf
testing_tools/build/debug/bt_native_pair_trial.o: testing_tools/src/diagnostics/bt_native_events.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_NATIVE_EVENT_DISCOVERY=1 -DBT_NATIVE_EVENT_TRACE=1 -DBT_NATIVE_CONNECT_TRIAL=1 -DBT_NATIVE_PAIR_TRIAL=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-pair-trial.elf: testing_tools/build/debug/bt_native_pair_trial.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-connect-trial: testing_tools/dist/ps5-xinput-bt-native-connect-trial.elf
testing_tools/build/debug/bt_native_connect_trial.o: testing_tools/src/diagnostics/bt_native_events.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_NATIVE_EVENT_DISCOVERY=1 -DBT_NATIVE_EVENT_TRACE=1 -DBT_NATIVE_CONNECT_TRIAL=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-connect-trial.elf: testing_tools/build/debug/bt_native_connect_trial.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-event-trace: testing_tools/dist/ps5-xinput-bt-native-event-trace.elf
testing_tools/build/debug/bt_native_event_trace.o: testing_tools/src/diagnostics/bt_native_events.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_NATIVE_EVENT_DISCOVERY=1 -DBT_NATIVE_EVENT_TRACE=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-event-trace.elf: testing_tools/build/debug/bt_native_event_trace.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-event-discovery: testing_tools/dist/ps5-xinput-bt-native-event-discovery.elf
testing_tools/build/debug/bt_native_event_discovery.o: testing_tools/src/diagnostics/bt_native_events.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_NATIVE_EVENT_DISCOVERY=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-event-discovery.elf: testing_tools/build/debug/bt_native_event_discovery.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-events: testing_tools/dist/ps5-xinput-bt-native-events.elf
testing_tools/dist/ps5-xinput-bt-native-events.elf: testing_tools/build/debug/bt_native_events.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
.PHONY: bt-native-discovery
bt-native-discovery: testing_tools/dist/ps5-xinput-bt-native-discovery.elf
testing_tools/build/debug/bt_native_discovery.o: testing_tools/src/diagnostics/bt_native_query.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_NATIVE_DISCOVERY=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-discovery.elf: testing_tools/build/debug/bt_native_discovery.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-query: testing_tools/dist/ps5-xinput-bt-native-query.elf
testing_tools/dist/ps5-xinput-bt-native-query.elf: testing_tools/build/debug/bt_native_query.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-client-probe: testing_tools/dist/ps5-xinput-bt-client-probe.elf
.PHONY: bt-registration-probe
.PHONY: bt-kernel-access-probe
bt-kernel-access-probe: testing_tools/dist/ps5-xinput-bt-kernel-access.elf
testing_tools/dist/ps5-xinput-bt-kernel-access.elf: testing_tools/build/debug/bt_kernel_access_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-registration-probe: testing_tools/dist/ps5-xinput-bt-registration-probe.elf
testing_tools/dist/ps5-xinput-bt-registration-probe.elf: testing_tools/build/debug/bt_registration_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
testing_tools/dist/ps5-xinput-bt-client-probe.elf: testing_tools/build/debug/bt_client_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
.PHONY: bt-exports
.PHONY: bt-unknown-exports
bt-unknown-exports: testing_tools/dist/ps5-xinput-bt-unknown-exports.elf
testing_tools/build/debug/bt_unknown_exports.o: testing_tools/src/diagnostics/bt_exports.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -Itesting_tools/.tools/sdk-source/crt -DXINPUT_VERBOSE=1 -DBT_EXPORT_CAPTURE_UNKNOWN=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-unknown-exports.elf: testing_tools/build/debug/bt_unknown_exports.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-exports: testing_tools/dist/ps5-xinput-bt-exports.elf
testing_tools/build/debug/bt_exports.o: testing_tools/src/diagnostics/bt_exports.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -Itesting_tools/.tools/sdk-source/crt -DXINPUT_VERBOSE=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-exports.elf: testing_tools/build/debug/bt_exports.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-inventory: testing_tools/dist/ps5-xinput-bt-inventory.elf
testing_tools/dist/ps5-xinput-bt-inventory.elf: testing_tools/build/debug/bt_inventory.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-load-probe: testing_tools/dist/ps5-xinput-bt-load-probe.elf
testing_tools/dist/ps5-xinput-bt-load-probe.elf: testing_tools/build/debug/bt_load_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-library-probe: testing_tools/dist/ps5-xinput-bt-library-probe.elf
testing_tools/build/debug/bt_library_probe.o: testing_tools/src/diagnostics/bt_probe.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -Wno-unused-function -DXINPUT_VERBOSE=1 -DBT_LIBRARY_ONLY=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-library-probe.elf: testing_tools/build/debug/bt_library_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-native-probe: testing_tools/dist/ps5-xinput-bt-native-probe.elf
testing_tools/build/debug/bt_native_probe.o: testing_tools/src/diagnostics/bt_probe.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -Wno-unused-function -DXINPUT_VERBOSE=1 -DBT_NATIVE_ONLY=1 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-native-probe.elf: testing_tools/build/debug/bt_native_probe.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^
bt-secondary-probe: testing_tools/dist/ps5-xinput-bt-secondary-probe.elf
testing_tools/build/debug/bt_hci_secondary.o: testing_tools/src/bluetooth/bt_hci_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=1 -DBT_HCI_FUNCTION=2 -c $< -o $@
testing_tools/dist/ps5-xinput-bt-secondary-probe.elf: testing_tools/build/debug/bt_radio_probe.o testing_tools/build/debug/bt_hci_secondary.o testing_tools/build/debug/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

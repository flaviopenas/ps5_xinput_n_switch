# Explicit firmware test variant; default 4.03 objects/artifacts stay separate.
.PHONY: manager-1360
manager-1360: testing_tools/dist/ps5-xinput-manager-1360.elf testing_tools/dist/ps5-xinput-service-1360.elf
	sha256sum $^ > testing_tools/dist/SHA256SUMS.manager-1360
	python3 testing_tools/scripts/package_manager.py --firmware 1360

testing_tools/build/1360/vpad.o: testing_tools/src/platform/vpad_variants.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/1360
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DVPAD_FW_1360=1 -c $< -o $@

testing_tools/dist/ps5-xinput-service-1360.elf: testing_tools/build/release/manager_service.o testing_tools/build/release/usb_service.o testing_tools/build/release/gip.o testing_tools/build/release/switch_usb.o testing_tools/build/release/manager_http.o testing_tools/build/release/bt_store.o testing_tools/build/release/bt_session.o testing_tools/build/release/bt_classic.o testing_tools/build/release/bt_xbox.o testing_tools/build/release/bt_timing.o testing_tools/build/release/bt_hog.o testing_tools/build/release/bt_hci.o testing_tools/build/release/bt_advert.o testing_tools/build/release/log.o testing_tools/build/1360/vpad.o testing_tools/build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/manager_service_1360_blob.h: testing_tools/dist/ps5-xinput-service-1360.elf scripts/embed_assets.py
	python3 scripts/embed_assets.py $@ --elf $<

testing_tools/build/1360/manager_launcher.o: testing_tools/src/app/manager_launcher_variants.c testing_tools/build/manager_service_1360_blob.h $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/1360
	$(CC) $(CFLAGS) -Itesting_tools/build -DXINPUT_VERBOSE=0 -DVPAD_FW_1360=1 -c $< -o $@

testing_tools/dist/ps5-xinput-manager-1360.elf: testing_tools/build/1360/manager_launcher.o testing_tools/build/release/log.o
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/manager_assets.h: manager/index.html manager/app.css manager/app.js scripts/embed_assets.py
	python3 scripts/embed_assets.py $@

testing_tools/build/release/manager_service.o: testing_tools/src/app/manager_service_variants.c testing_tools/build/manager_assets.h $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/release
	$(CC) $(CFLAGS) -Itesting_tools/build -DXINPUT_VERBOSE=0 -DMANAGER_USB=1 -c $< -o $@

.PHONY: native-baseline
native-baseline: testing_tools/dist/ps5-xinput-native-baseline.elf
testing_tools/build/debug/manager_native_baseline.o: testing_tools/src/app/manager_service_variants.c testing_tools/build/manager_assets.h $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) -Itesting_tools/build -DXINPUT_VERBOSE=1 -DMANAGER_USB=1 -DMANAGER_BT_MANUAL_START=1 -c $< -o $@
testing_tools/dist/ps5-xinput-native-baseline.elf: testing_tools/build/debug/manager_native_baseline.o testing_tools/build/release/usb_service.o testing_tools/build/release/gip.o testing_tools/build/release/switch_usb.o testing_tools/build/release/manager_http.o testing_tools/build/release/bt_store.o testing_tools/build/release/bt_session.o testing_tools/build/release/bt_classic.o testing_tools/build/release/bt_xbox.o testing_tools/build/release/bt_timing.o testing_tools/build/release/bt_hog.o testing_tools/build/release/bt_hci.o testing_tools/build/release/bt_advert.o testing_tools/build/release/log.o testing_tools/build/release/vpad.o testing_tools/build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p testing_tools/dist
	$(CC) -o $@ $^

testing_tools/build/release/usb_service.o: testing_tools/src/usb/usb_bridge_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DUSB_SERVICE=1 -c $< -o $@

testing_tools/build/release/bt_session.o: testing_tools/src/bluetooth/bt_session_legacy.c $(PROJECT_HEADERS)
	@mkdir -p testing_tools/build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DBT_HID_CAPTURE=1 -DBT_BRIDGE=1 -DBT_SERVICE=1 -c $< -o $@

testing_tools/build/release/bt_xbox.o: src/bluetooth/bt_xbox_map.inc

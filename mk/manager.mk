build/manager_assets.h: manager/index.html manager/app.css manager/app.js scripts/embed_assets.py
	python3 scripts/embed_assets.py $@

build/release/manager_service.o: src/app/manager_service.c build/manager_assets.h $(PROJECT_HEADERS)
	@mkdir -p build/release
	$(CC) $(CFLAGS) -Ibuild -DXINPUT_VERBOSE=0 -DMANAGER_USB=1 -c $< -o $@

build/release/usb_service.o: src/usb/usb_bridge.c $(PROJECT_HEADERS)
	@mkdir -p build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DUSB_SERVICE=1 -c $< -o $@

build/release/bt_session.o: src/bluetooth/bt_session.c $(PROJECT_HEADERS)
	@mkdir -p build/release
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -DBT_HID_CAPTURE=1 -DBT_BRIDGE=1 -DBT_SERVICE=1 -c $< -o $@

build/release/bt_xbox.o: src/bluetooth/bt_xbox_map.inc

build/ps5-xinput-service.elf: build/release/manager_service.o build/release/usb_service.o build/release/gip.o build/release/switch_usb.o build/release/manager_http.o build/release/bt_store.o build/release/bt_session.o build/release/bt_classic.o build/release/bt_xbox.o build/release/bt_timing.o build/release/bt_hog.o build/release/bt_hci.o build/release/bt_advert.o build/release/log.o build/release/vpad.o build/release/user.o $(BT_CRYPTO_OBJS)
	@mkdir -p build
	$(CC) -o $@ $^

build/manager_service_blob.h: build/ps5-xinput-service.elf scripts/embed_assets.py
	python3 scripts/embed_assets.py $@ --elf $<

build/release/manager_launcher.o: src/app/manager_launcher.c build/manager_service_blob.h $(PROJECT_HEADERS)
	@mkdir -p build/release
	$(CC) $(CFLAGS) -Ibuild -DXINPUT_VERBOSE=0 -c $< -o $@

dist/ps5-xinput-manager.elf: build/release/manager_launcher.o build/release/log.o
	@mkdir -p dist
	$(CC) -o $@ $^

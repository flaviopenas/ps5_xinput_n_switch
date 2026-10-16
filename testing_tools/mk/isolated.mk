# Current 4.03 transports in separate processes, with independent log ports.
ISOLATED_COMMON := log vpad user gip switch_usb
ISOLATED_BT := manager_http bt_store bt_session bt_classic bt_xbox bt_timing bt_hog bt_hci bt_advert
ISOLATED_COMMON_OBJS := $(addprefix testing_tools/build/isolated/,$(addsuffix .o,$(ISOLATED_COMMON)))
ISOLATED_BT_OBJS := $(addprefix testing_tools/build/isolated/,$(addsuffix .o,$(ISOLATED_BT)))

.PHONY: bluetooth-only usb-only
bluetooth-only: testing_tools/dist/ps5-xinput-bluetooth-only.elf
usb-only: testing_tools/dist/ps5-xinput-usb-only.elf

testing_tools/build/isolated/%.o: %.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@

testing_tools/build/isolated/bt_session.o: CFLAGS += -DBT_SERVICE=1 -DBT_BRIDGE=1 -DBT_HID_CAPTURE=1
testing_tools/build/isolated/bt_xbox.o: src/bluetooth/bt_xbox_map.inc
testing_tools/build/isolated/usb_bridge.o: CFLAGS += -DUSB_SERVICE=1

testing_tools/build/isolated/manager_bluetooth_only.o: testing_tools/src/app/manager_bluetooth_only.c src/app/manager_service.c testing_tools/build/manager_assets.h $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Itesting_tools/build -DXINPUT_VERBOSE=0 -c $< -o $@

testing_tools/build/isolated/usb_only.o: testing_tools/src/app/usb_only.c $(PROJECT_HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DXINPUT_VERBOSE=0 -c $< -o $@

testing_tools/dist/ps5-xinput-bluetooth-only.elf: testing_tools/build/isolated/manager_bluetooth_only.o $(ISOLATED_COMMON_OBJS) $(ISOLATED_BT_OBJS) $(BT_CRYPTO_OBJS)
	@mkdir -p $(@D)
	$(CC) -o $@ $^

testing_tools/dist/ps5-xinput-usb-only.elf: testing_tools/build/isolated/usb_only.o testing_tools/build/isolated/usb_bridge.o $(ISOLATED_COMMON_OBJS)
	@mkdir -p $(@D)
	$(CC) -o $@ $^

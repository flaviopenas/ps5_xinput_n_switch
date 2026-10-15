.PHONY: host-vpad-firmware
host-vpad-firmware:
	@mkdir -p testing_tools/build
	cc -std=c11 -Wall -Wextra -Werror -O2 $(PROJECT_INCLUDES) testing_tools/tests/vpad_firmware.c src/platform/vpad.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl -o testing_tools/build/vpad-firmware-403-test
	./testing_tools/build/vpad-firmware-403-test
	cc -std=c11 -Wall -Wextra -Werror -O2 $(PROJECT_INCLUDES) -DVPAD_FW_1360=1 testing_tools/tests/vpad_firmware.c testing_tools/src/platform/vpad_variants.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl -o testing_tools/build/vpad-firmware-1360-test
	./testing_tools/build/vpad-firmware-1360-test
	cc -D_DEFAULT_SOURCE -DGIP_HOST_TEST -DUSB_SERVICE -DVPAD_FW_1360=1 -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Itesting_tools/tests $(PROJECT_INCLUDES) testing_tools/tests/dual_controller.c testing_tools/tests/assignment_stub.c src/usb/gip.c src/usb/switch_usb.c testing_tools/src/platform/vpad_variants.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=access,--wrap=stat,--wrap=utimes,--wrap=unlink -o testing_tools/build/usb-service-1360-test
	./testing_tools/build/usb-service-1360-test

host-test: host-vpad-firmware host-bt-timing
	@mkdir -p testing_tools/build
	cc $(CFLAGS) testing_tools/tests/bt_classic.c src/bluetooth/bt_classic.c src/usb/switch_usb.c -o testing_tools/build/bt-classic-test
	./testing_tools/build/bt-classic-test
	cc $(CFLAGS) $(BT_CRYPTO_FLAGS) testing_tools/tests/bt_multi.c src/bluetooth/bt_timing.c testing_tools/tests/assignment_stub.c src/bluetooth/bt_classic.c src/bluetooth/bt_store.c src/usb/switch_usb.c src/bluetooth/bt_xbox.c src/bluetooth/bt_hog.c src/bluetooth/bt_advert.c src/platform/vpad.c src/bluetooth/bt_smp.c src/bluetooth/bt_crypto.c third_party/tiny-aes/aes.c third_party/micro-ecc/uECC.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl -ldl -o testing_tools/build/bt-multi-test
	./testing_tools/build/bt-multi-test
	@mkdir -p testing_tools/build
	cc -D_DEFAULT_SOURCE -DGIP_HOST_TEST -DUSB_SERVICE -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Itesting_tools/tests $(PROJECT_INCLUDES) testing_tools/tests/dual_controller.c testing_tools/tests/assignment_stub.c src/usb/gip.c src/usb/switch_usb.c src/platform/vpad.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=access,--wrap=stat,--wrap=utimes,--wrap=unlink -o testing_tools/build/usb-service-test
	./testing_tools/build/usb-service-test
	@mkdir -p testing_tools/build
	cc -std=c11 -Wall -Wextra -Werror $(PROJECT_INCLUDES) testing_tools/tests/switch_usb.c src/usb/switch_usb.c src/platform/vpad.c testing_tools/tests/assignment_stub.c -o testing_tools/build/switch-usb-test
	./testing_tools/build/switch-usb-test
	@mkdir -p testing_tools/build
	python3 scripts/embed_assets.py testing_tools/build/manager_assets.h
	python3 testing_tools/tests/manager.py
	cc $(CFLAGS) testing_tools/tests/bt_xbox.c testing_tools/tests/assignment_stub.c src/bluetooth/bt_xbox.c src/platform/vpad.c -o testing_tools/build/bt-xbox-test
	./testing_tools/build/bt-xbox-test
	cc $(CFLAGS) testing_tools/tests/bt_hog.c src/bluetooth/bt_hog.c -o testing_tools/build/bt-hog-test
	./testing_tools/build/bt-hog-test
	cc $(CFLAGS) $(BT_CRYPTO_FLAGS) testing_tools/tests/bt_crypto_vectors.c src/bluetooth/bt_crypto.c third_party/tiny-aes/aes.c -o testing_tools/build/bt-crypto-vectors-test
	./testing_tools/build/bt-crypto-vectors-test
	cc $(CFLAGS) $(BT_CRYPTO_FLAGS) testing_tools/tests/bt_smp.c src/bluetooth/bt_smp.c src/bluetooth/bt_crypto.c third_party/tiny-aes/aes.c third_party/micro-ecc/uECC.c -o testing_tools/build/bt-smp-test
	./testing_tools/build/bt-smp-test
	cc -std=c11 -Wall -Wextra -Werror $(PROJECT_INCLUDES) testing_tools/tests/bt_advert.c src/bluetooth/bt_advert.c -o testing_tools/build/bt-advert-test
	./testing_tools/build/bt-advert-test
	cc -std=c11 -Wall -Wextra -Werror $(PROJECT_INCLUDES) testing_tools/tests/mapping.c testing_tools/tests/assignment_stub.c src/platform/vpad.c -o testing_tools/build/mapping-test
	./testing_tools/build/mapping-test
	cc -std=c11 -Wall -Wextra -Werror $(PROJECT_INCLUDES) testing_tools/tests/vpad_status.c testing_tools/tests/assignment_stub.c src/platform/vpad.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl -o testing_tools/build/vpad-status-test
	./testing_tools/build/vpad-status-test
	cc -D_DEFAULT_SOURCE -DGIP_HOST_TEST -std=c11 -Wall -Wextra -Werror -Itesting_tools/tests $(PROJECT_INCLUDES) testing_tools/tests/dual_controller.c testing_tools/tests/assignment_stub.c src/usb/gip.c src/usb/switch_usb.c src/platform/vpad.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=access -o testing_tools/build/dual-controller-test
	./testing_tools/build/dual-controller-test
	./testing_tools/build/dual-controller-test mixed
	./testing_tools/build/dual-controller-test switch
	cc -D_DEFAULT_SOURCE -DGIP_HOST_TEST -DXINPUT_VERBOSE=1 -std=c11 -Wall -Wextra -Werror -Itesting_tools/tests $(PROJECT_INCLUDES) testing_tools/tests/dual_controller.c testing_tools/tests/assignment_stub.c src/usb/gip.c src/usb/switch_usb.c src/platform/vpad.c -Wl,--wrap=open,--wrap=close,--wrap=ioctl,--wrap=access -o testing_tools/build/dual-controller-debug-test
	./testing_tools/build/dual-controller-debug-test
	./testing_tools/build/dual-controller-debug-test mixed
	./testing_tools/build/dual-controller-debug-test switch
	cc -D_DEFAULT_SOURCE -std=c11 -Wall -Wextra -Werror $(PROJECT_INCLUDES) testing_tools/tests/assignment_events.c testing_tools/src/diagnostics/assignment.c -ldl -o testing_tools/build/assignment-events-test
	./testing_tools/build/assignment-events-test
	cc -D_DEFAULT_SOURCE -DSHELL_BIND_HOST_TEST -DENABLE_EXPERIMENTAL_SHELL_BIND -std=c11 -Wall -Wextra -Werror -Itesting_tools/tests $(PROJECT_INCLUDES) testing_tools/tests/shell_bind_cleanup.c testing_tools/src/diagnostics/shell_bind.c -o testing_tools/build/shell-bind-test
	./testing_tools/build/shell-bind-test

.PHONY: host-bt-timing
host-bt-timing:
	@mkdir -p testing_tools/build
	cc $(CFLAGS) testing_tools/tests/bt_timing.c src/bluetooth/bt_timing.c -o testing_tools/build/bt-timing-test
	./testing_tools/build/bt-timing-test

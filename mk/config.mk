PS5_PAYLOAD_SDK ?= $(CURDIR)/.tools/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

SOURCE_DIRS := src/app src/platform src/usb src/bluetooth
PROJECT_INCLUDES := $(addprefix -I,$(SOURCE_DIRS))
BUILD_CONFIG := Makefile $(wildcard mk/*.mk)
PROJECT_HEADERS := $(wildcard $(addsuffix /*.h,$(SOURCE_DIRS))) $(BUILD_CONFIG)
vpath %.c $(SOURCE_DIRS)

CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 $(PROJECT_INCLUDES)
BT_CRYPTO_FLAGS := -DECB=1 -DCBC=0 -DCTR=0 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0
BT_CRYPTO_OBJS := build/release/bt_crypto.o build/release/bt_smp.o build/release/aes.o build/release/uECC.o
build/release/bt_crypto.o build/release/bt_smp.o: CFLAGS += $(BT_CRYPTO_FLAGS)

build/release/aes.o: third_party/tiny-aes/aes.c third_party/tiny-aes/aes.h
	@mkdir -p build/debug
	$(CC) $(CFLAGS) $(BT_CRYPTO_FLAGS) -c $< -o $@
build/release/uECC.o: third_party/micro-ecc/uECC.c $(wildcard third_party/micro-ecc/*)
	@mkdir -p build/debug
	$(CC) $(CFLAGS) $(BT_CRYPTO_FLAGS) -c $< -o $@

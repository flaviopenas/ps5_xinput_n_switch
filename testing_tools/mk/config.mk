PS5_PAYLOAD_SDK ?= $(CURDIR)/.tools/ps5-payload-sdk
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

SOURCE_DIRS := src/app src/platform src/usb src/bluetooth testing_tools/src/diagnostics
PROJECT_INCLUDES := $(addprefix -I,$(SOURCE_DIRS))
BUILD_CONFIG := testing_tools/Makefile $(wildcard testing_tools/mk/*.mk)
PROJECT_HEADERS := $(wildcard $(addsuffix /*.h,$(SOURCE_DIRS))) $(BUILD_CONFIG)
vpath %.c $(SOURCE_DIRS)

CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 $(PROJECT_INCLUDES)
BT_CRYPTO_FLAGS := -DECB=1 -DCBC=0 -DCTR=0 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 -DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0
BT_CRYPTO_OBJS := testing_tools/build/debug/bt_crypto.o testing_tools/build/debug/bt_smp.o testing_tools/build/debug/aes.o testing_tools/build/debug/uECC.o
testing_tools/build/debug/bt_crypto.o testing_tools/build/debug/bt_smp.o: CFLAGS += $(BT_CRYPTO_FLAGS)

testing_tools/build/debug/aes.o: third_party/tiny-aes/aes.c third_party/tiny-aes/aes.h
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) $(BT_CRYPTO_FLAGS) -c $< -o $@
testing_tools/build/debug/uECC.o: third_party/micro-ecc/uECC.c $(wildcard third_party/micro-ecc/*)
	@mkdir -p testing_tools/build/debug
	$(CC) $(CFLAGS) $(BT_CRYPTO_FLAGS) -c $< -o $@
COMMON_SRCS := src/usb/usb_bridge.c src/usb/gip.c src/usb/switch_usb.c src/platform/vpad.c src/platform/log.c src/platform/user.c
DEBUG_SRCS := $(COMMON_SRCS) testing_tools/src/diagnostics/assignment.c testing_tools/src/diagnostics/shell_bind.c testing_tools/src/diagnostics/pad_abi.c
RELEASE_OBJS := $(addprefix testing_tools/build/release/,$(notdir $(COMMON_SRCS:.c=.o)))
DEBUG_OBJS := $(addprefix testing_tools/build/debug/,$(notdir $(DEBUG_SRCS:.c=.o)))


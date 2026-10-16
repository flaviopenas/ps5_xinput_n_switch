PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
ifneq ($(CURDIR),$(PROJECT_ROOT))
.DEFAULT_GOAL := all
.PHONY: all $(MAKECMDGOALS)
all $(MAKECMDGOALS):
	$(MAKE) -C $(PROJECT_ROOT) -f testing_tools/Makefile $@
else
.DEFAULT_GOAL := host-test
include testing_tools/mk/config.mk
include testing_tools/mk/common.mk
include testing_tools/mk/manager.mk
include testing_tools/mk/usb.mk
include testing_tools/mk/bluetooth.mk
include testing_tools/mk/isolated.mk
include testing_tools/mk/diagnostics.mk
include testing_tools/mk/tests.mk
include testing_tools/mk/quality.mk
endif

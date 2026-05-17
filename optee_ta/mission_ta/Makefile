# Makefile for the PX4 Mission Verification Trusted Application
#
# Build:
#   export TA_DEV_KIT_DIR=/home/jetson/Linux_for_Tegra/source/optee/optee_os/out/arm-plat-tegra/export-ta_arm64
#   make
#
# Deploy (requires sudo):
#   sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /lib/optee_armtz/
#   sudo cp 8aaaf200-2450-11e4-abe2-0002a5d5c51b.ta /usr/lib/optee_armtz/

BINARY = 8aaaf200-2450-11e4-abe2-0002a5d5c51b

# Use the aarch64 cross-compiler (native on Jetson, so just use the system gcc)
CROSS_COMPILE ?= aarch64-linux-gnu-

# Log level: 0=none 1=err 2=info 3=debug 4=flow
CFG_TEE_TA_LOG_LEVEL ?= 2
CPPFLAGS += -DCFG_TEE_TA_LOG_LEVEL=$(CFG_TEE_TA_LOG_LEVEL)

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk),)
$(error TA_DEV_KIT_DIR not set or invalid. Export it before running make:\
  export TA_DEV_KIT_DIR=/home/jetson/Linux_for_Tegra/source/optee/optee_os/out/arm-plat-tegra/export-ta_arm64)
endif

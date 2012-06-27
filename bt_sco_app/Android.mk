ifeq ($(BOARD_HAVE_BLUETOOTH),true)
LOCAL_PATH:= $(call my-dir)

#
# bt_sco_app
#

include $(CLEAR_VARS)

LOCAL_C_INCLUDES:= \
	$(call include-path-for, bluez)/lib/

LOCAL_CFLAGS:= \
	-DVERSION=\"4.96\"

LOCAL_SRC_FILES:= \
	bt_sco_app.c

LOCAL_SHARED_LIBRARIES := \
	libbluetooth  libcutils

LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE:=bt_sco_app

include $(BUILD_EXECUTABLE)

endif

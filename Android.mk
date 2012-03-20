#wpan utilties and TI ST user space manager
include $(call first-makefiles-under,$(call my-dir))


LOCAL_PATH := $(call my-dir)
ifeq ($(BLUETI_ENHANCEMENT), true)

#install main.conf
include $(CLEAR_VARS)
LOCAL_MODULE := main.conf
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/bluetooth
LOCAL_SRC_FILES := ../../system/bluetooth/data/main.conf
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)
endif

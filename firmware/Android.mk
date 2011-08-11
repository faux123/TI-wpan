LOCAL_PATH := $(call my-dir)

# 128x PG-2.21 service pack 39
include $(CLEAR_VARS)
LOCAL_MODULE := TIInit_10.6.15.bts
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/firmware/
LOCAL_SRC_FILES := bluetooth/TIInit_10.6.15.39.bts
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)


# 128x PG-2.0
include $(CLEAR_VARS)
LOCAL_MODULE := TIInit_10.4.27.bts
LOCAL_MODULE_CLASS := ETC
LOCAL_MODULE_PATH := $(TARGET_OUT)/etc/firmware/
LOCAL_SRC_FILES := bluetooth/TIInit_10.4.27.22.bts
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)

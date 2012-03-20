LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#
# ANT enabler Application
#

LOCAL_C_INCLUDES:= ant_enabler.h

LOCAL_SRC_FILES:= \
	ant_enabler.c
LOCAL_PRELINK_MODULE := false


LOCAL_CFLAGS:= -g -c -W -Wall -O2 -D_POSIX_SOURCE
LOCAL_C_INCLUDES:=\
	$(call include-path-for, bluez)/src \
	$(call include-path-for, bluez)/lib


LOCAL_SHARED_LIBRARIES:= libbluetoothd libbluetooth
LOCAL_MODULE:=ant_enabler
LOCAL_MODULE_TAGS:= eng

include $(BUILD_EXECUTABLE)

LOCAL_PATH:= $(call my-dir)

# libselinux.so (stub)
include $(CLEAR_VARS)
LOCAL_MODULE:= libselinux
LOCAL_SRC_FILES := selinux_stub.c
include $(BUILD_SHARED_LIBRARY)

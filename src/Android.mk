LOCAL_PATH := $(call my-dir)

# binary
include $(CLEAR_VARS)
LOCAL_MODULE := tinysu
LOCAL_SRC_FILES := daemon/tinysu.c
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/daemon \
	$(LOCAL_PATH)/external
LOCAL_SHARED_LIBRARIES := libselinux
LOCAL_LDLIBS := -llog
include $(BUILD_EXECUTABLE)

# lib
include external/Android.mk

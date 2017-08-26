LOCAL_PATH := $(call my-dir)

# binary
include $(CLEAR_VARS)
LOCAL_MODULE := tinysu
LOCAL_SRC_FILES := daemon/tinysu.cpp daemon/daemon.cpp daemon/client.cpp
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/daemon
LOCAL_LDLIBS := -llog
LOCAL_CFLAGS := -DARM
LOCAL_CPPFLAGS := -std=c++11
include $(BUILD_EXECUTABLE)

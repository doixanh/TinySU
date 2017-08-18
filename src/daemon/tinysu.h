/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

#define TINYSU_VER 1
#define TINYSU_VER_STR "0.1"

#define TINYSU_PORT 12385
#define MAX_CLIENT 100

#define ARG_LEN 10240
#define DEFAULT_SHELL "/system/bin/sh"

#define CLIENT "TinySUClient"
#define DAEMON "TinySUDaemon"

#define LogI(x, y, args...) __android_log_print(ANDROID_LOG_INFO, x, y, ## args)
#define LogV(x, y, args...) __android_log_print(ANDROID_LOG_VERBOSE, x, y, ## args)
#define LogE(x, y, args...) __android_log_print(ANDROID_LOG_ERROR, x, y, ## args)


/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

#define TINYSU_VER 1
#define TINYSU_VER_STR "0.1"

#define TINYSU_PORT 12385
#define MAX_CLIENT 100
#define END_COMMAND EOF\0

#define ARG_LEN 10240
#define CLIENT "TinySUClient"
#define DAEMON "TinySUDaemon"

#ifdef ARM
    #define DEFAULT_SHELL "/system/bin/sh"
#else
    #define DEFAULT_SHELL "/bin/sh"
#endif


#ifdef ARM
    #define LogI(x, y, args...) __android_log_print(ANDROID_LOG_INFO, x, y, ## args)
    #define LogV(x, y, args...) __android_log_print(ANDROID_LOG_VERBOSE, x, y, ## args)
    #define LogE(x, y, args...) __android_log_print(ANDROID_LOG_ERROR, x, y, ## args)
#else
    #define VERBOSE(x)  x
    #define ERROR(x)    x
    #define LogI(x, y, args...) printf("I/[%10s] " y "\n", x, ## args)
    #define LogV(x, y, args...) VERBOSE(printf("V/[%10s] " y "\n", x, ## args))
    #define LogE(x, y, args...) ERROR(printf("E/[%10s] " y "\n", x, ## args))
#endif

typedef struct client {
    int fd;
    int pid;
    int in[2];
    int out[2];
} client_t;
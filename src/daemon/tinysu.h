/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

#include <errno.h>
#ifdef ARM
#include <android/log.h>
#endif

#define TINYSU_VER 2
#define TINYSU_VER_STR "0.2"

#ifdef ARM
#define TINYSU_SOCKET_PATH (char*) "/su/tinysu"
#define TINYSU_SOCKET_ERR_PATH (char*) "/su/tinysu.err"
#else
#define TINYSU_SOCKET_PATH (char*) "/tmp/tinysu"
#define TINYSU_SOCKET_ERR_PATH (char*) "/tmp/tinysu.err"
#endif

#define MAX_CLIENT 100

#define ARG_LEN 10240
#define CLIENT (char*) "TinySUClient"
#define DAEMON (char*) "TinySUDaemon"

#define ACTOR_STDIN (char*) "stdin"
#define ACTOR_STDOUT (char*) "stdout"
#define ACTOR_STDERR (char*) "stderr"
#define ACTOR_CLIENT (char*) "Client"
#define ACTOR_DAEMON (char*) "Daemon"
#define ACTOR_CHILD (char*) "Child"

#define AUTH_TIMEOUT 15
#define AUTH_OK (char*) "YaY!"

#ifdef ARM
    #define DEFAULT_SHELL (char*) "/system/bin/sh"
#else
    #define DEFAULT_SHELL (char*) "/bin/sh"
#endif


#ifdef ARM
    #define LogI(x, y, args...) __android_log_print(ANDROID_LOG_INFO, x, y, ## args)
    #define LogV(x, y, args...) __android_log_print(ANDROID_LOG_VERBOSE, x, y, ## args)
    #define LogE(x, y, args...) __android_log_print(ANDROID_LOG_ERROR, x, y, ## args)
#else
    #define VERBOSE(x)
    #define ERROR(x)    x
    #define LogI(x, y, args...) printf("I/[%10s] " y "\n", x, ## args)
    #define LogV(x, y, args...) VERBOSE(printf("V/[%10s] " y "\n", x, ## args))
    #define LogE(x, y, args...) ERROR(printf("E/[%10s] " y "\n", x, ## args))
#endif

// struct definitions
typedef struct client {
    int fd;
    int errFd;
    int pid;
    int in[2];
    int out[2];
    int err[2];
    int died;
} client_t;

// shared variables
extern client_t clients[MAX_CLIENT];
auto nothing = [](int from){};

// utility functions
void doClose(int fd);
void markNonblock(int fd);
void getActorNameByFd(int fd, char *actorName, char *logPrefix);

// function definitions
/**
 * Read all possible data from one file descriptor and write to the other
 */
template <typename F> void proxy(int from, int to, F onerror) {
    char s[1024];
    char actorName[32];
    char log[16];
    bool firstLoop = true;
    while (true) {
        memset(s, 0, sizeof(s));
        ssize_t numRead = read(from, s, sizeof(s) - 1);
        if (numRead < 0) {
            if (errno != EAGAIN) {
                onerror(from);
            }
            break;
        }
        else if (numRead == 0) {
            if (firstLoop) {
                onerror(from);
            }
            break;
        }

        getActorNameByFd(from, actorName, log);
        LogV(log, "%s says %s", actorName, s);

        write(to, s, (size_t) numRead);
        firstLoop = false;
    }
}
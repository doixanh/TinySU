/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

#define TINYSU_VER 1
#define TINYSU_VER_STR "0.1"

#define TINYSU_PORT 12385
#define MAX_CLIENT 100

#define ARG_LEN 10240
#define SHELL "/bin/bash"

#define VERBOSE(x)
#define ERROR(x)    x

#define CLIENT "Client"
#define DAEMON "Daemon"

#define LogI(x, y, args...) printf("I/[%10s] " y "\n", x, ## args)
#define LogV(x, y, args...) VERBOSE(printf("V/[%10s] " y "\n", x, ## args))
#define LogE(x, y, args...) ERROR(printf("E/[%10s] " y "\n", x, ## args))
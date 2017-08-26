/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

/*
 * We have two modes: daemon and client
 *  - daemon must be execute with root right (i.e. with prerooted insecure kernels)
 *  - client: pass the parameter to daemon and wait for response, write it to stdout
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#include "tinysu.h"
#include "daemon.h"
#include "client.h"

client_t clients[MAX_CLIENT];
char *shell = nullptr;

void doClose(int fd) {
    // LogV(DAEMON, "Closing fd %d", fd);
    close(fd);
}
/**
 * Mark a certain file descriptor nonblocking
 */
void markNonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    fl |= O_NONBLOCK;
    fcntl(fd, F_SETFL, fl);
}

/**
 * Get actor name from an FD
 */
void getActorNameByFd(int fd, char *actorName, char *logPrefix) {
    actorName[0] = 0;
    logPrefix[0] = 0;
    if (fd == STDIN_FILENO) {
        strcat(actorName, ACTOR_STDIN);
        strcat(logPrefix, CLIENT);
    }
    else if (fd == STDOUT_FILENO) {
        strcat(actorName, ACTOR_STDOUT);
        strcat(logPrefix, DAEMON);
    }
    else if (fd == STDERR_FILENO) {
        strcat(actorName, ACTOR_STDERR);
        strcat(logPrefix, DAEMON);
    }
    else {
        for (int i = 0; i < MAX_CLIENT; i++) {
            if (clients[i].fd) {
                if (fd == clients[i].fd) {
                    sprintf(actorName, "%s %d", ACTOR_CLIENT, fd);
                    strcat(logPrefix, DAEMON);
                    break;
                }
                else if (fd == clients[i].out[0]) {
                    sprintf(actorName, "%s %d", ACTOR_CHILD, clients[i].pid);
                    strcat(logPrefix, DAEMON);
                }
            }
        }
    }
    if (!strlen(actorName)) {
        strcat(actorName, ACTOR_DAEMON);
        strcat(logPrefix, CLIENT);
    }
}

/**
 * Show some help.
 */
void printUsage(char *self) {
    printf("This is TinySU ver %s by doixanh.\n", TINYSU_VER_STR);
    printf("https://github.com/doixanh/TinySU\n");
    printf("Usage: %s -hdvV [-c command]\n", self);
    exit(0);
}

/**
 * The FUN starts here :)
 */
int main(int argc, char **argv) {
    setbuf(stdout, nullptr);
    int opt = 0;
    /*LogV(CLIENT, "Running su parameters:");
    for (int i = 0; i < argc; i++) {
        LogV(CLIENT, "- %s", argv[i]);
    }*/
    while ((opt = getopt(argc, argv, "hdvVc:s:")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                break;
            case 'd':
                goDaemonMode();
                break;
            case 'V':
                printf("%d\n", TINYSU_VER);
                exit(0);
            case 'v':
                printf("%s\n", TINYSU_VER_STR);
                exit(0);
            case 'c':
                goCommandMode(argc, argv);
                exit(0);
            case 's':
                shell = optarg;
                break;
            default: /* '?' */
                printUsage(argv[0]);
        }
    }
    goInteractiveMode();
}

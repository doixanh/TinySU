/*
 * This is 'su' source, on the C side.
 * Licensed with GPLv3.
 */

/*
 * We have two modes: daemon and launch
 *  - daemon must be execute with root right (i.e. with prerooted insecure kernels)
 *  - launch as normal
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>

#include "tinysu.h"

/**
 * Show some help.
 */
void printUsage(char *self) {
    printf("This is TinySU.\n");
    printf("Usage: %s -hdvV [-c command]\n", self);
    exit(1);
}

/**
 * Init a listening TCP socket
 */
int initSocket() {
    int sockfd, clen, clientfd;
    struct sockaddr_in saddr, caddr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error creating socket\n");
        exit(1);
    }

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = htons(TINYSU_PORT);
    if (bind(sockfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
        printf("Error binding socket\n");
        exit(1);
    }

    if (listen(sockfd, 5) < 0) {
        printf("Error listening to socket\n");
        exit(1);
    }
    return sockfd;
}

/**
 * Accept incoming connection
 */
void acceptClients(int sockfd) {
    struct sockaddr_in caddr;
    unsigned int clen = sizeof(caddr);
    int clientfd;

    if ((clientfd = accept(sockfd, (struct sockaddr *) &caddr, &clen)) < 0) {
        printf("Error accepting connection\n");
    }
}

/**
 * Go into background, listen and accept incoming su requests
 */
void goDaemonMode() {
    printf("I'm going to daemon mode.\n");
    int sockfd = initSocket();
    acceptClients(sockfd);
    exit(0);
}

int main(int argc, char **argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hdvV")) != -1) {
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
            default: /* '?' */
                printUsage(argv[0]);
        }
    }
    printUsage(argv[0]);
}

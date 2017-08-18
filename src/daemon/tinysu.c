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
#include <netinet/ip.h>
#include <sys/fcntl.h>
#include <sys/errno.h>

#include "tinysu.h"



int clientfds[MAX_CLIENT];

/**
 * Show some help.
 */
void printUsage(char *self) {
    LogI(DAEMON, "This is TinySU ver %s.", TINYSU_VER_STR);
    LogI(DAEMON, "Usage: %s -hdvV [-c command]", self);
    exit(1);
}

/**
 * Init a listening TCP socket
 */
int initSocket() {
    int sockfd, clen, clientfd;
    struct sockaddr_in saddr, caddr;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LogE(DAEMON, "Error creating socket");
        exit(1);
    }

    // make it reusable
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int));

    // make it nonblocking
    int fl;
    fl = fcntl(sockfd, F_GETFL, 0);
    fl |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, fl);

    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = htons(TINYSU_PORT);
    if (bind(sockfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
        LogE(DAEMON, "Error binding socket");
        exit(1);
    }

    if (listen(sockfd, 5) < 0) {
        LogE(DAEMON, "Error listening to socket");
        exit(1);
    }

    // init the clientfds array
    memset(clientfds, 0, sizeof(clientfds));

    return sockfd;
}

/**
 * Accept incoming connection
 */
void acceptClients(int sockfd) {
    struct sockaddr_in caddr;
    unsigned int clen = sizeof(caddr);
    fd_set readset;
    int i;
    int fl;
    int clientfd;
    char s[1024];
    struct timeval timeout;

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = 10;

    // wait for max 10s for incoming su command

    LogI(DAEMON, "Accepting clients on sock %d", sockfd);

    while (clen > 0) {
        FD_ZERO(&readset);                    // clear the set
        FD_SET(sockfd, &readset);            // add listening socket to the set

        int maxfd = sockfd;                    // maxfd will be used for select()

        // add all clientfds into the reading set
        for (i = 0; i < MAX_CLIENT; i++) {
            if (clientfds[i] > 0) {
                FD_SET(clientfds[i], &readset);    // add a FD into the set
                if (clientfds[i] > maxfd) maxfd = clientfds[i];
            }
        }

        // pool and wait for 10s max
        int selectVal = select(maxfd + 1, &readset, NULL, NULL, &timeout);
        if (selectVal < 0) {
            perror("select");
            exit(1);
        }

        // is that an incoming connection from the listening socket?
        if (FD_ISSET(sockfd, &readset)) {
            memset(&caddr, 0, sizeof(caddr));
            clientfd = accept(sockfd, (struct sockaddr *) &caddr, &clen);
            if (clientfd >= 0) {
                LogI(DAEMON, "New client %d", clientfd);

                // make it nonblock as well
                fl = fcntl(clientfd, F_GETFL, 0);
                fl |= O_NONBLOCK;
                fcntl(clientfd, F_SETFL, fl);

                // add it to the clientfds array so that we can include it in client fdset
                for (i = 0; i < MAX_CLIENT; i++) {
                    if (clientfds[i] == 0) {
                        clientfds[i] = clientfd;
                        break;
                    }
                }
            }
        }

        // is that data from a previously-connect client?
        for (i = 0; i < MAX_CLIENT; i++) {
            if (clientfds[i] > 0 && FD_ISSET(clientfds[i], &readset)) {
                memset(s, 0, sizeof(s));
                if (read(clientfds[i], s, sizeof(s)) > 0) {
                    LogI(DAEMON, " - Client %d says: %s", clientfds[i], s);

                    // fork. execute.
                    int execpid = fork();
                    if (execpid == 0) {
                        // we are child.
                        char *argv[4];
                        int argc = 0;
                        argv[argc++] = SHELL;
                        argv[argc++] = "-c";
                        argv[argc++] = s;

                        // we use this NULL to indicate termination of the list argv
                        argv[argc] = NULL;

                        // now that we are child, redirect our stdout/stderr to clientfd
                        clientfd = clientfds[i];
                        dup2(clientfd, fileno(stdout));
                        dup2(clientfd, fileno(stderr));
                        execvp(argv[0], argv);

                        // if code goes here, meaning we have problems with execvp
                        LogE(DAEMON, "Error execvp");
                        exit(1);
                    }
                    else {
                        LogI(DAEMON, " - Parent is waiting for pid %d", execpid);

                        // ok we are parent. wait for child to finish
                        waitpid(execpid, NULL, 0);


                        LogI(DAEMON, " - Disconnecting client %d after exec()", clientfds[i]);

                        // and close
                        shutdown(clientfds[i], SHUT_RDWR);
                        close(clientfds[i]);
                        clientfds[i] = 0;
                    }
                }
                else {
                    // some error. remove it from the "active" fd array
                    LogI(DAEMON, " - Client %d has disconnected.", clientfds[i]);
                    shutdown(clientfds[i], SHUT_RDWR);
                    close(clientfds[i]);
                    clientfds[i] = 0;
                }
            }
        }
    }
    // disconnect
    close(sockfd);
}

/**
 * Go into background, listen and accept incoming su requests
 */
void goDaemonMode() {
    LogI(DAEMON, "This is TinySU ver %s.", TINYSU_VER_STR);
    LogI(DAEMON, "Operating in daemon mode.");
    int sockfd = initSocket();
    acceptClients(sockfd);
    exit(0);
}

/**
 * Connect to the daemon, pass cmd and return the response to stdout
 */
void goClientMode(int argc, char **argv) {
    struct sockaddr_in saddr;
    int sockfd;
    char buf[1024];
    char cmd[ARG_LEN];
    fd_set readset;
    struct timeval timeout;

    // concat argv
    memset(cmd, 0, sizeof(cmd));
    for (int i = optind - 1; i < argc; i++) {
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }
    LogV(CLIENT, "Passing cmd '%s'", cmd);

    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = 10;

    // create the socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LogE(CLIENT, "Error creating socket");
        exit(1);
    }

    // init sockaddr
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = htons(TINYSU_PORT);

    // connect to server
    if (connect(sockfd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        LogE(CLIENT, "Cannot connect to daemon");
        close(sockfd);
        exit(1);
    }

    // make it nonblocking
    int fl;
    fl = fcntl(sockfd, F_GETFL, 0);
    fl |= O_NONBLOCK;
    fcntl(sockfd, F_SETFL, fl);

    LogV(CLIENT, "Connected successfully!");
    LogV(CLIENT, "Sending command %s", cmd);
    write(sockfd, cmd, strlen(cmd) + 1);

    while (1) {
        FD_ZERO(&readset);                    // clear the set
        FD_SET(sockfd, &readset);            // add listening socket to the set

        int maxfd = sockfd;                    // maxfd will be used for select()

        // pool and wait for 10s max
        int selectVal = select(maxfd + 1, &readset, NULL, NULL, &timeout);
        if (selectVal < 0) {
            perror("select");
            exit(1);
        }

        // is that an incoming connection from the listening socket?
        if (FD_ISSET(sockfd, &readset)) {
            memset(buf, 0, sizeof(buf));
            ssize_t res = read(sockfd, buf, sizeof(buf) - 1);  // preserve at least one last char for NULL termination
            if (res > 0) {
                printf("%s", buf);
            }
            else {
                break;
            }
        }
    }
    close(sockfd);
}

/**
 * The FUN starts here :)
 */
int main(int argc, char **argv) {
    int opt = 0;
    while ((opt = getopt(argc, argv, "hdvVc:")) != -1) {
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
                goClientMode(argc, argv);
                exit(0);
            default: /* '?' */
                printUsage(argv[0]);
        }
    }
    printUsage(argv[0]);
}

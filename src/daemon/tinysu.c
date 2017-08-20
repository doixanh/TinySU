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
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef ARM
#include <selinux/selinux.h>
#include <android/log.h>
#endif

#include "tinysu.h"



struct Client clients[MAX_CLIENT];
char *shell = NULL;

/**
 * Show some help.
 */
void printUsage(char *self) {
    printf("This is TinySU ver %s.\n", TINYSU_VER_STR);
    printf("Usage: %s -hdvV [-c command]\n", self);
    exit(0);
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

    // init arrays
    memset(clients, 0, sizeof(clients));

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
    int clientpid;
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
            if (clients[i].fd > 0) {
                FD_SET(clients[i].fd, &readset);    // add a FD into the set
                if (clients[i].fd > maxfd) maxfd = clients[i].fd;
                FD_SET(clients[i].out[0], &readset);
                if (clients[i].out[0] > maxfd) maxfd = clients[i].out[0];
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

                // add it to the client array so that we can include it in client fdset
                int clientIdx = -1;
                for (i = 0; i < MAX_CLIENT; i++) {
                    if (clients[i].fd == 0) {
                        clients[i].fd = clientfd;
                        pipe(clients[i].in);
                        pipe(clients[i].out);
                        clientIdx = i;
                        break;
                    }
                }

                // pre-fork
                clientpid = fork();
                if (clientpid == 0) {
                    // we are child.
                    char *argv[2];
                    int argc = 0;
                    argv[argc++] = DEFAULT_SHELL;
                    argv[argc] = NULL;

                    // redirect
                    dup2(clients[i].in[0], fileno(stdin));           // child input to stdin pipe 0
                    dup2(clients[i].out[1], fileno(stdout));         // child output to stdout pipe 1
                    dup2(clients[i].out[1], fileno(stderr));         // child err to stdout pipe 1

                    setenv("HOME", "/sdcard", 1);
                    setenv("SHELL", DEFAULT_SHELL, 1);
                    setenv("USER", "root", 1);
                    setenv("LOGNAME", "root", 1);
#ifdef ARM
                    setexeccon("u:r:su:s0");
#endif
                    execvp(argv[0], argv);

                    // if code goes here, meaning we have problems with execvp
                    LogE(DAEMON, "Error execvp");
                    exit(1);
                }
                else {
                    // save pid to the list
                    clients[clientIdx].pid = clientpid;
                }
            }
        }

        for (i = 0; i < MAX_CLIENT; i++) {
            // is that data from a child? forward to the client
            if (clients[i].fd > 0 && FD_ISSET(clients[i].out[0], &readset)) {
                memset(s, 0, sizeof(s));
                ssize_t numread = read(clients[i].out[0], s, sizeof(s));
                LogI(DAEMON, " - Child %d says: %s", clients[i].pid, s);
                write(clients[i].fd, s, numread);
            }

            // is that data from a previously-connect client?
            if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, &readset)) {
                memset(s, 0, sizeof(s));
                ssize_t numread = read(clients[i].fd, s, sizeof(s));
                if (numread > 0) {
                    LogI(DAEMON, " - Client %d says: %s", clients[i].fd, s);
                    strcat(s, "\n");
                    // write through the pipe to the child...
                    write(clients[i].in[1], s, numread + 1);
                }
                else {
                    // some error. remove it from the "active" fd array
                    LogI(DAEMON, " - Client %d has disconnected.", clients[i].fd);
                    shutdown(clients[i].fd, SHUT_RDWR);
                    close(clients[i].fd);

                    // kill the child
                    kill(clients[i].pid, SIGTERM);

                    clients[i].fd = 0;
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
 * Connect to the daemon
 * @return sockfd
 */
int connectToDaemon() {
    struct sockaddr_in saddr;
    int sockfd;

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
    return sockfd;
}

/**
 * Send a single command to the daemon, wait for response
 */
void sendCommand(int sockfd, char * cmd) {
    char buf[1024];
    fd_set readset;
    struct timeval timeout;
    memset(&timeout, 0, sizeof(timeout));
    timeout.tv_sec = 1;

    if (cmd != NULL) {
        LogV(CLIENT, " - SendCommand: Sending command %s", cmd);
        write(sockfd, cmd, strlen(cmd) + 1);
    }

    int selectStdin = 1;

    while (1) {
        FD_ZERO(&readset);                     // clear the set
        FD_SET(sockfd, &readset);              // add listening socket to the set

        if (selectStdin) {
            FD_SET(fileno(stdin), &readset);       // add stdin to the set
        }

        int maxfd = sockfd;                    // maxfd will be used for select()

        // pool and wait for 10s max
        int selectVal = select(maxfd + 1, &readset, NULL, NULL, &timeout);
        if (selectVal < 0) {
            // timed out
            break;
        }

        // anything happened?
        if (selectVal > 0) {
            // is that from stdin? send to the socket
            if (cmd == NULL && FD_ISSET(fileno(stdin), &readset)) {
                memset(buf, 0, sizeof(buf));
                fgets(buf, sizeof(buf), stdin);
                if (strlen(buf) > 0) {
                    LogI(CLIENT, "Got command from stdin %s", buf);
                    write(sockfd, buf, strlen(buf));
                }
                else {
                    // end of the output. Don't poll stdin anymore.
                    selectStdin = 0;
                }
            }

            // is that an incoming connection from the listening socket?
            if (FD_ISSET(sockfd, &readset)) {
                memset(buf, 0, sizeof(buf));
                ssize_t res = read(sockfd, buf, sizeof(buf) - 1);  // preserve at least one last char for NULL termination
                if (res > 0) {
                    printf("%s", buf);
                }
                else {
                    // nothing to read. server has disconnected.
                    break;
                }
            }
        }
        else if (selectVal == 0) {
            // no more data from the socket AND from stdin.
            // safe to break
            break;
        }
    }
    fflush(stdout);
}

/**
 * Connect to the daemon, pass cmd and return the response to stdout
 */
void goCommandMode(int argc, char **argv) {
    LogI(CLIENT, "CommandMode: Going command mode.");
    char cmd[ARG_LEN];

    // concat argv
    memset(cmd, 0, sizeof(cmd));
    for (int i = optind - 1; i < argc; i++) {
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }
    int sockfd = connectToDaemon();
    usleep(200*1000);
    sendCommand(sockfd, cmd);
    close(sockfd);
}

/**
 * Go to interactive mode
 */
void goInteractiveMode() {
    LogI(CLIENT, "Interactive: Going interactive mode.");
    int sockfd = connectToDaemon();
    sendCommand(sockfd, NULL);
    close(sockfd);
}

/**
 * The FUN starts here :)
 */
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    int opt = 0;
    LogV(CLIENT, "Running su parameters:");
    for (int i = 0; i < argc; i++) {
        LogV(CLIENT, "- %s", argv[i]);
    }
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

    // go interactive mode
    goInteractiveMode();
}

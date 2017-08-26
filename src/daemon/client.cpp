//
// Created by dx on 8/26/17.
//
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <sys/un.h>

#include "tinysu.h"

int clientId;
int daemonFd;
int daemonErrFd;

/**
 * Connect to the daemon, both on regular port and stderr port
 */
void connectToDaemon() {
    char s[16];
    struct sockaddr_un saddr;

    // create the socket
    if ((daemonFd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        LogE(CLIENT, "Error creating socket");
        exit(1);
    }

    // init sockaddr
    memset(&saddr, 0, sizeof(saddr));
    saddr.sun_family = AF_UNIX;
    strcpy(saddr.sun_path, TINYSU_SOCKET_PATH);

    // connect to server
    if (connect(daemonFd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        LogE(CLIENT, "Cannot connect to daemon at %s. Error %s", TINYSU_SOCKET_PATH, strerror(errno));
        doClose(daemonFd);
        exit(1);
    }
    // LogV(CLIENT, "daemonFd=%d", daemonFd);

    // wait for our id
    memset(s, 0, sizeof(s));
    read(daemonFd, s, sizeof(s));
    clientId = atoi(s);
    // LogI(CLIENT, "Our id is %d", clientId);

    // create the stderr socket
    if ((daemonErrFd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        LogE(CLIENT, "Error creating err socket");
        exit(1);
    }

    // init sockaddr
    memset(&saddr, 0, sizeof(saddr));
    saddr.sun_family = AF_UNIX;
    strcpy(saddr.sun_path, TINYSU_SOCKET_ERR_PATH);

    // connect to server on stderr socket
    if (connect(daemonErrFd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        LogE(CLIENT, "Cannot connect to daemon err at %s. Error %s", TINYSU_SOCKET_ERR_PATH, strerror(errno));
        perror("connect");
        doClose(daemonErrFd);
        exit(1);
    }
    write(daemonErrFd, s, strlen(s));

    // make them nonblocking
    markNonblock(daemonFd);
    markNonblock(daemonErrFd);

    LogV(CLIENT, "Connected successfully!");
}

/**
 * Send command to server and wait for response.
 * @param cmd command to send. if nullptr, get command from stdin
 */
void sendCommand(int daemonFd, char *cmd) {
    fd_set readSet;
    struct timeval timeout = {};
    memset(&timeout, 0, sizeof(timeout));

    if (cmd != nullptr) {
        // LogV(CLIENT, " - SendCommand: Sending command %s", cmd);
        strcat(cmd, "\nexit\n");
        write(daemonFd, cmd, strlen(cmd));
    }
    else {
        // make stdin nonblocking
        markNonblock(STDIN_FILENO);
    }

    bool connected = true;
    while (connected) {
        // prepare readSet
        FD_ZERO(&readSet);                      // clear the set
        FD_SET(daemonFd, &readSet);             // add listening socket to the set
        FD_SET(daemonErrFd, &readSet);          // add listening socket to the set
        FD_SET(STDIN_FILENO, &readSet);         // add stdin to the set
        int maxFd = daemonFd > daemonErrFd ? daemonFd : daemonErrFd;

        // pool and wait
        timeout.tv_sec = 3600;
        int selectVal = select(maxFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (selectVal < 0) {
            // error
            break;
        }

        // anything happened?
        if (selectVal > 0) {
            // is that from stdin? send to the socket
            if (cmd == nullptr && FD_ISSET(STDIN_FILENO, &readSet)) {
                proxy(STDIN_FILENO, daemonFd, nothing);
            }
            // is that an incoming connection from the connected socket?
            if (FD_ISSET(daemonFd, &readSet)) {
                // forward to stdout
                proxy(daemonFd, STDOUT_FILENO, [&connected](int from) {
                    // LogI(CLIENT, "Daemon has just disconnected us :(");
                    connected = false;
                });
            }
            // is that an incoming connection from the connected stderr socket?
            if (FD_ISSET(daemonErrFd, &readSet)) {
                // forward to stderr
                proxy(daemonErrFd, STDERR_FILENO, [&connected](int from) {
                    // LogI(CLIENT, "Daemon has just disconnected us :(");
                    connected = false;
                });
            }
        }
        else if (selectVal == 0) {
            // LogI(CLIENT, "Nothing is ready for select()");
        }
    }
    fflush(stdout);
}

/**
 * Connect to the daemon, pass cmd and return the response to stdout
 */
void goCommandMode(int argc, char **argv) {
    LogI(CLIENT, "CommandMode: Going command mode. PPID=%d", getppid());
    char cmd[ARG_LEN];

    // concat argv
    memset(cmd, 0, sizeof(cmd));
    for (int i = optind - 1; i < argc; i++) {
        strcat(cmd, argv[i]);
        strcat(cmd, " ");
    }
    connectToDaemon();
    usleep(200*1000);
    sendCommand(daemonFd, cmd);
    doClose(daemonFd);
}

/**
 * Go to interactive mode
 */
void goInteractiveMode() {
    LogI(CLIENT, "Interactive: Going interactive mode. PPID=%d", getppid());
    connectToDaemon();
    sendCommand(daemonFd, nullptr);
    doClose(daemonFd);
}
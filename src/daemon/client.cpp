//
// Created by dx on 8/26/17.
//
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>

#include "tinysu.h"
#include "client.h"

/**
 * Connect to the daemon
 * @return sockfd
 */
int connectToDaemon() {
    struct sockaddr_in saddr = {};
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
        doClose(sockfd);
        exit(1);
    }

    // make it nonblocking
    markNonblock(sockfd);

    LogV(CLIENT, "Connected successfully!");
    return sockfd;
}

/**
 * Send command to server and wait for response.
 * @param cmd command to send. if nullptr, get command from stdin
 */
void sendCommand(int sockfd, char * cmd) {
    fd_set readset;
    struct timeval timeout = {};
    memset(&timeout, 0, sizeof(timeout));

    if (cmd != nullptr) {
        LogV(CLIENT, " - SendCommand: Sending command %s", cmd);
        strcat(cmd, "\nexit\n");
        write(sockfd, cmd, strlen(cmd));
    }
    else {
        // make stdin nonblocking
        markNonblock(STDIN_FILENO);
    }

    bool connected = true;
    while (connected) {
        // prepare readset
        FD_ZERO(&readset);                      // clear the set
        FD_SET(sockfd, &readset);               // add listening socket to the set
        FD_SET(STDIN_FILENO, &readset);         // add stdin to the set

        // pool and wait
        timeout.tv_sec = 5;
        int selectVal = select(sockfd + 1, &readset, nullptr, nullptr, &timeout);
        if (selectVal < 0) {
            // error
            break;
        }

        // anything happened?
        if (selectVal > 0) {
            // is that from stdin? send to the socket
            if (cmd == nullptr && FD_ISSET(STDIN_FILENO, &readset)) {
                proxy(STDIN_FILENO, sockfd, nothing);
            }
            // is that an incoming connection from the connected socket?
            if (FD_ISSET(sockfd, &readset)) {
                // forward to stdout
                proxy(sockfd, STDOUT_FILENO, [&connected](int from) {
                    LogI(CLIENT, "Daemon has just disconnected us :(");
                    connected = false;
                });
            }
        }
        else if (selectVal == 0) {
            LogI(CLIENT, "Nothing is ready for select()");
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
    int sockfd = connectToDaemon();
    usleep(200*1000);
    sendCommand(sockfd, cmd);
    doClose(sockfd);
}

/**
 * Go to interactive mode
 */
void goInteractiveMode() {
    LogI(CLIENT, "Interactive: Going interactive mode. PPID=%d", getppid());
    int sockfd = connectToDaemon();
    sendCommand(sockfd, nullptr);
    doClose(sockfd);
}
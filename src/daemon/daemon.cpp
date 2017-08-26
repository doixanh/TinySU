//
// Created by dx on 8/26/17.
//

#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/ip.h>
#include <errno.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "tinysu.h"

int listenFd;
int listenErrFd;

/**
 * Signal handler. Mainly used to process SIGCHLD from children
 */
void handleSignals(int signum, siginfo_t *info, void *ptr)  {
    if (signum == SIGCHLD) {
        for (int i = 0; i < MAX_CLIENT; i++) {
            if (clients[i].pid == info->si_pid) {
                // LogI(DAEMON, " - Child %d is killed. ", clients[i].pid);
                clients[i].died = 1;
                break;
            }
        }
    }
}

/**
 * Register signal handler to process child exits
 */
void registerSignalHandler() {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handleSignals;
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGCHLD, &act, nullptr);
    // LogV(DAEMON, "Registered signal handler.");
}

/**
 * Init a listening TCP socket
 */
int initListeningSocket(char *path) {
    int sockfd;
    struct sockaddr_un saddr;
    umask(0);
    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        LogE(DAEMON, "Error creating socket");
        exit(1);
    }

    // make it reusable
    int one = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // make it nonblocking
    markNonblock(sockfd);

    memset(&saddr, 0, sizeof(saddr));
    saddr.sun_family = AF_UNIX;
    strcpy(saddr.sun_path, path);
    unlink(path);

    if (bind(sockfd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
        LogE(DAEMON, "Error binding socket %s", path);
        perror("bind");
        exit(1);
    }

    if (listen(sockfd, 5) < 0) {
        LogE(DAEMON, "Error listening to socket");
        exit(1);
    }
    return sockfd;
}

/**
 * Disconnect all clients that are associated with 'marked' died children
 * Closing the pipes to the children too.
 */
void disconnectDeadClients() {
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].died) {
            // LogI(DAEMON, " - Child %d died, disconnecting client %d", clients[i].pid, clients[i].fd);
            close(clients[i].in[0]);
            close(clients[i].in[1]);
            close(clients[i].out[0]);
            close(clients[i].out[1]);
            close(clients[i].err[0]);
            close(clients[i].err[1]);
            close(clients[i].fd);
            close(clients[i].errFd);
            // LogI(DAEMON, " - Closing following fds: in [%d %d] out [%d %d] err [%d %d] sock [%d %d]", clients[i].in[0], clients[i].in[1], clients[i].out[0], clients[i].out[1], clients[i].err[0], clients[i].err[1], clients[i].fd, clients[i].errFd);
            clients[i].died = 0;
            clients[i].pid = 0;
            clients[i].fd = 0;
        }
    }
}

/**
 * Add all possible file descriptors to a readset for later select()
 */
int addDaemonFdsToReadset(fd_set *readset) {
    FD_ZERO(readset);                     // clear the set
    FD_SET(listenFd, readset);            // add listening socket to the set
    FD_SET(listenErrFd, readset);         // add listening socket to the set
    int maxfd = listenFd;
    if (maxfd < listenErrFd) {
        maxfd = listenErrFd;
    }

    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].fd > 0 && !clients[i].died) {
            FD_SET(clients[i].fd, readset);    // add a FD into the set
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
            FD_SET(clients[i].out[0], readset);
            if (clients[i].out[0] > maxfd) maxfd = clients[i].out[0];
            FD_SET(clients[i].err[0], readset);
            if (clients[i].err[0] > maxfd) maxfd = clients[i].err[0];
        }
    }
    return maxfd;
}

/**
 * Add a clientfd to the #clients list
 * Create pipes to communicate with its corresponding child
 */
int addClientToList(int clientFd) {
    markNonblock(clientFd);

    // add it to the client array so that we can include it in client fdset
    int clientIdx = -1;
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].fd == 0) {
            clients[i].fd = clientFd;
            pipe(clients[i].in);
            pipe(clients[i].out);
            pipe(clients[i].err);

            markNonblock(clients[i].in[0]);
            markNonblock(clients[i].out[0]);
            markNonblock(clients[i].err[0]);

            clientIdx = i;
            break;
        }
    }
    return clientIdx;
}

/**
 * Exec a shell to serve a client. Redirect stdin/stdout/stderr of the child to 3 pipes.
 * We will forward these data to the client using these pipes.
 */
void execShell(int clientIdx) {
    char *argv[2];
    int argc = 0;
    argv[argc++] = DEFAULT_SHELL;
    argv[argc] = nullptr;

    // redirect
    dup2(clients[clientIdx].in[0], STDIN_FILENO);           // child input to stdin pipe 0
    dup2(clients[clientIdx].out[1], STDOUT_FILENO);         // child output to stdout pipe 1
    dup2(clients[clientIdx].err[1], STDERR_FILENO);         // child err to stderr pipe 1

    setenv("HOME", "/sdcard", 1);
    setenv("SHELL", DEFAULT_SHELL, 1);
    setenv("USER", "root", 1);
    setenv("LOGNAME", "root", 1);
    execvp(argv[0], argv);

    // if code goes here, meaning we have problems with execvp
    LogE(DAEMON, "Error execvp");
    exit(1);
}

/**
 * Forward data between children and clients
 */
void forwardData(fd_set *readSet) {
    for (int i = 0; i < MAX_CLIENT; i++) {
        // is that data from a child stdout? forward to the client stdout
        if (clients[i].fd > 0 && FD_ISSET(clients[i].out[0], readSet)) {
            // forward to the client
            proxy(clients[i].out[0], clients[i].fd, nothing);
        }

        // is that data from a child stderr? forward to the client stderr
        if (clients[i].fd > 0 && FD_ISSET(clients[i].err[0], readSet)) {
            // forward to the client
            proxy(clients[i].err[0], clients[i].errFd, nothing);
        }

        // is that data from a previously-connect client?
        if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, readSet)) {
            // forward to the corresponding child's stdin
            proxy(clients[i].fd, clients[i].in[1], [](int from) {
                // some error. remove it from the "active" fd array
                // LogI(DAEMON, " - Client %d has disconnected.", from);
                shutdown(from, SHUT_RDWR);
                // we dont close from here, we will do it in disconnectDeadClients();

                // find the child
                for (int j = 0; j < MAX_CLIENT; j++) {
                    if (clients[j].fd == from) {
                        // kill the child
                        kill(clients[j].pid, SIGKILL);
                        clients[j].died = 1;
                        break;
                    }
                }
            });
        }
    }
}

/**
 * Accept incoming connections
 */
void acceptClient(int listenFd) {
    char s[16];
    struct sockaddr_in caddr;
    unsigned int clen = sizeof(caddr);
    int clientFd;
    int clientPid;
    memset(&caddr, 0, sizeof(caddr));
    clientFd = accept(listenFd, (struct sockaddr *) &caddr, &clen);
    if (clientFd > 0) {
        // LogI(DAEMON, "New client %d", clientFd);

        // welcome with its id
        memset(s, 0, sizeof(s));
        sprintf(s, "%d", clientFd);
        write(clientFd, s, strlen(s));

        int clientIdx = addClientToList(clientFd);

        // pre-fork
        clientPid = fork();
        if (clientPid == 0) {
            // we are child.
            execShell(clientIdx);
        }
        else {
            // parent. save pid to the list
            clients[clientIdx].pid = clientPid;
        }
    }
}

/**
 * Accept incoming connections
 */
void acceptClientErr(int listenErrFd) {
    char s[16];
    struct sockaddr_in caddr;
    unsigned int clen = sizeof(caddr);
    int clientId;
    int clientErrFd;
    memset(&caddr, 0, sizeof(caddr));
    clientErrFd = accept(listenErrFd, (struct sockaddr *) &caddr, &clen);
    if (clientErrFd > 0) {
        // LogI(DAEMON, "New connection for stderr %d", clientErrFd);

        // wait for id from client
        memset(s, 0, sizeof(s));
        read(clientErrFd, s, sizeof(s));
        clientId = atoi(s);

        for (int i = 0; i < MAX_CLIENT; i++) {
            if (clients[i].fd == clientId) {
                // LogV(DAEMON, "Matching clientErrFd %d with clientFd %d", clientErrFd, clientId);
                clients[i].errFd = clientErrFd;
                return;
            }
        }
    }

}

/**
 * Wait and serve all clients
 */
void serveClients(int listenFd, int listenErrFd) {
    fd_set readSet;
    struct timeval timeout = {10, 0};
    memset(&timeout, 0, sizeof(timeout));
    registerSignalHandler();
    // LogI(DAEMON, "Serving clients on sock %d and sockErr %d", listenFd, listenErrFd);

    while (true) {
        int maxfd = addDaemonFdsToReadset(&readSet);

        // pool and wait
        timeout.tv_sec = 3600;
        int selectVal = select(maxfd + 1, &readSet, nullptr, nullptr, &timeout);
        if (selectVal < 0) {
            if (errno != EINTR) {
                perror("select");
                exit(1);
            }
            else {
                // select was interrupted, probably by SIGCHLD.
                disconnectDeadClients();
            }
        }
        else if (selectVal == 0) {
            // LogI(DAEMON, "Nothing for daemon select()");
        }
        else if (selectVal > 0) {
            // is that an incoming connection from the listening socket?
            if (FD_ISSET(listenFd, &readSet)) {
                acceptClient(listenFd);
            }
            // is that an incoming connection from the listening socket for stderr?
            if (FD_ISSET(listenErrFd, &readSet)) {
                acceptClientErr(listenErrFd);
            }
            // is that data from children or clients?
            forwardData(&readSet);
        }
        disconnectDeadClients();
    }
}

/**
 * Go into background, listen and accept incoming su requests
 */
void goDaemonMode() {
    LogI(DAEMON, "This is TinySU ver %s.", TINYSU_VER_STR);
    LogI(DAEMON, "Operating in daemon mode.");
    memset(clients, 0, sizeof(clients));
    listenFd = initListeningSocket(TINYSU_SOCKET_PATH);
    listenErrFd = initListeningSocket(TINYSU_SOCKET_ERR_PATH);
    serveClients(listenFd, listenErrFd);
}
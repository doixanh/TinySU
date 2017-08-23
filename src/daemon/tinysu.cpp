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
#include <errno.h>

#ifdef ARM
#include <selinux/selinux.h>
#include <android/log.h>
#endif

#include "tinysu.h"

client_t clients[MAX_CLIENT];
char *shell = nullptr;

auto nothing = [](int from){};

/**
 * Mark a certain file descriptor nonblocking
 */
void markNonblock(int fd) {
    // make it nonblock as well
    int fl = fcntl(fd, F_GETFL, 0);
    fl |= O_NONBLOCK;
    fcntl(fd, F_SETFL, fl);
}

/**
 * Read all possible data from one file descriptor and write to the other
 */
template <typename F> void proxy(int from, int to, char *logPrefix, char *fromActor, F onerror) {
    char s[1024];
    while (true) {
        memset(s, 0, sizeof(s));
        ssize_t numRead = read(from, s, sizeof(s));
        if (numRead < 0) {
            if (errno != EAGAIN && onerror != nullptr) {
                onerror(from);
            }
            break;
        }
        else if (numRead == 0) {
            break;
        }

        if (fromActor) {
            LogI(logPrefix, "%s says %s", fromActor, s);
        }
        write(to, s, (size_t) numRead);
    }
}

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
    struct sockaddr_in saddr = {};
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LogE(DAEMON, "Error creating socket");
        exit(1);
    }

    // make it reusable
    int one = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    // make it nonblocking
    markNonblock(sockfd);

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
 * Disconnect all clients that are associated with 'marked' died children
 * Closing the pipes to the children too.
 */
void disconnectDeadClient() {
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].died) {
            close(clients[i].in[0]);
            close(clients[i].in[1]);
            close(clients[i].out[0]);
            close(clients[i].out[1]);
            close(clients[i].fd);
            clients[i].died = 0;
            clients[i].fd = 0;
        }
    }
}
/**
 * Signal handler. Mainly used to process SIGCHLD from children
 */
void handleSignals(int signum, siginfo_t *info, void *ptr)  {
    if (signum == SIGCHLD) {
        for (int i = 0; i < MAX_CLIENT; i++) {
            if (clients[i].pid == info->si_pid) {
                LogI(DAEMON, "Child %d is killed. ", clients[i].pid);
                clients[i].died = 1;
                break;
            }
        }
    }
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
    struct timeval timeout = {10, 0};

    memset(&timeout, 0, sizeof(timeout));

    // register signal handler to process child exits
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = handleSignals;
    act.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGCHLD, &act, nullptr);
    LogV(DAEMON, "Registered signal handler.");

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
        timeout.tv_sec = 10;
        int selectVal = select(maxfd + 1, &readset, nullptr, nullptr, &timeout);
        if (selectVal < 0) {
            if (errno != EINTR) {
                perror("select");
                exit(1);
            }
            else {
                // select was interrupted, probably by SIGCHLD.
                disconnectDeadClient();
            }
        }

        if (selectVal == 0) {
            LogI(DAEMON, "Nothing for daemon select()");
        }
        else if (selectVal > 0) {
            // is that an incoming connection from the listening socket?
            if (FD_ISSET(sockfd, &readset)) {
                memset(&caddr, 0, sizeof(caddr));
                clientfd = accept(sockfd, (struct sockaddr *) &caddr, &clen);
                if (clientfd >= 0) {
                    LogI(DAEMON, "New client %d", clientfd);

                    // make it nonblock as well
                    markNonblock(clientfd);

                    // add it to the client array so that we can include it in client fdset
                    int clientIdx = -1;
                    for (i = 0; i < MAX_CLIENT; i++) {
                        if (clients[i].fd == 0) {
                            clients[i].fd = clientfd;
                            pipe(clients[i].in);
                            pipe(clients[i].out);

                            markNonblock(clients[i].in[0]);
                            markNonblock(clients[i].out[0]);

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
                        argv[argc] = nullptr;

                        // redirect
                        dup2(clients[i].in[0], STDIN_FILENO);           // child input to stdin pipe 0
                        dup2(clients[i].out[1], STDOUT_FILENO);         // child output to stdout pipe 1
                        dup2(clients[i].out[1], STDERR_FILENO);         // child err to stdout pipe 1

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
                    } else {
                        // save pid to the list
                        clients[clientIdx].pid = clientpid;
                    }
                }
            }

            for (i = 0; i < MAX_CLIENT; i++) {
                // is that data from a child? forward to the client
                if (clients[i].fd > 0 && FD_ISSET(clients[i].out[0], &readset)) {
                    // forward to the client
                    proxy(clients[i].out[0], clients[i].fd, DAEMON, (char*) "Child", nothing);
                }

                // is that data from a previously-connect client?
                if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, &readset)) {
                    // forward to the corresponding child's stdin
                    proxy(clients[i].fd, clients[i].in[1], DAEMON, (char*) "Client", [](int from) {
                        // some error. remove it from the "active" fd array
                        LogI(DAEMON, " - Client %d has disconnected.", from);
                        shutdown(from, SHUT_RDWR);
                        close(from);

                        // find the child
                        for (int j = 0; j < MAX_CLIENT; j++) {
                            if (clients[j].fd == from) {
                                // kill the child
                                kill(clients[j].pid, SIGKILL);
                                clients[j].fd = 0;
                                break;
                            }
                        }
                    });
                }
            }
        }

        disconnectDeadClient();
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
    memset(&clients, 0, sizeof(clients));
    acceptClients(sockfd);
    exit(0);
}

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
        close(sockfd);
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
    char buf[1024];
    fd_set readset;
    struct timeval timeout = {};
    memset(&timeout, 0, sizeof(timeout));

    if (cmd != nullptr) {
        LogV(CLIENT, " - SendCommand: Sending command %s", cmd);
        strcat(cmd, "\n");
        write(sockfd, cmd, strlen(cmd));
    }
    else {
        // make stdin nonblocking
        markNonblock(STDIN_FILENO);
    }

    while (true) {
        // prepare readset
        FD_ZERO(&readset);                     // clear the set
        FD_SET(sockfd, &readset);              // add listening socket to the set
        FD_SET(STDIN_FILENO, &readset);   // add stdin to the set

        // pool and wait for 1s max
        timeout.tv_sec = 50;
        int selectVal = select(sockfd + 1, &readset, nullptr, nullptr, &timeout);
        if (selectVal < 0) {
            // error
            break;
        }

        // anything happened?
        if (selectVal > 0) {
            // is that from stdin? send to the socket
            if (cmd == nullptr && FD_ISSET(STDIN_FILENO, &readset)) {
                proxy(STDIN_FILENO, sockfd, CLIENT, (char*) "Client stdin", nothing);
            }
            // is that an incoming connection from the listening socket?
            if (FD_ISSET(sockfd, &readset)) {
                proxy(sockfd, STDOUT_FILENO, CLIENT, (char*) "Daemon", [](int from) {
                    LogI(CLIENT, "Daemon has just disconnected us :(");
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
    close(sockfd);
}

/**
 * Go to interactive mode
 */
void goInteractiveMode() {
    LogI(CLIENT, "Interactive: Going interactive mode. PPID=%d", getppid());
    int sockfd = connectToDaemon();
    sendCommand(sockfd, nullptr);
    close(sockfd);
}

/**
 * The FUN starts here :)
 */
int main(int argc, char **argv) {
    setbuf(stdout, nullptr);
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

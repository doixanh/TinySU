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
int errSockfds[MAX_CLIENT];

char *shell = nullptr;

auto nothing = [](int from){};

void doClose(int fd) {
    LogV(DAEMON, "Closing fd %d", fd);
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
        LogI(log, "%s says %s", actorName, s);

        write(to, s, (size_t) numRead);
        firstLoop = false;
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
int initSocket(int port) {
    int sockfd;
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
    saddr.sin_port = htons(port);
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
void disconnectDeadClients() {
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].died) {
            LogI(DAEMON, "Child %d died, disconnecting client %d", clients[i].pid, clients[i].fd);
            close(clients[i].in[0]);
            close(clients[i].in[1]);
            close(clients[i].out[0]);
            close(clients[i].out[1]);
            close(clients[i].err[0]);
            close(clients[i].err[1]);
            close(clients[i].fd);
            LogI(DAEMON, "Closing following fds: %d %d %d %d %d", clients[i].in[0], clients[i].in[1], clients[i].out[0], clients[i].out[1], clients[i].err[0], clients[i].err[1], clients[i].fd);
            clients[i].died = 0;
            clients[i].pid = 0;
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
 * Add all possible file descriptors to a readset for later select()
 */
int addDaemonFdsToReadset(int sockfd, fd_set *readset) {
    int maxfd = 0;
    FD_ZERO(&readset);                    // clear the set
    FD_SET(sockfd, readset);              // add listening socket to the set

    // add all clientfds into the reading set
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].fd > 0) {
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
 * Create pipe to communicate with its corresponding child
 */
int addClientToList(int clientfd) {
    markNonblock(clientfd);

    // add it to the client array so that we can include it in client fdset
    int clientIdx = -1;
    for (int i = 0; i < MAX_CLIENT; i++) {
        if (clients[i].fd == 0) {
            clients[i].fd = clientfd;
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
    // we are child.
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
#ifdef ARM
    setexeccon("u:r:su:s0");
#endif
    execvp(argv[0], argv);

    // if code goes here, meaning we have problems with execvp
    LogE(DAEMON, "Error execvp");
    exit(1);
}

/**
 * Forward data between children and clients
 */
void daemonForward(fd_set *readset) {
    for (int i = 0; i < MAX_CLIENT; i++) {
        // is that data from a child stdout? forward to the client stdout
        if (clients[i].fd > 0 && FD_ISSET(clients[i].out[0], readset)) {
            // forward to the client
            proxy(clients[i].out[0], clients[i].fd, nothing);
        }

        // is that data from a child stderr? forward to the client stderr
        if (clients[i].fd > 0 && FD_ISSET(clients[i].err[0], readset)) {
            // forward to the client
            proxy(clients[i].err[0], clients[i].fd, nothing);
        }

        // is that data from a previously-connect client?
        if (clients[i].fd > 0 && FD_ISSET(clients[i].fd, readset)) {
            // forward to the corresponding child's stdin
            proxy(clients[i].fd, clients[i].in[1], [](int from) {
                // some error. remove it from the "active" fd array
                LogI(DAEMON, " - Client %d has disconnected.", from);
                shutdown(from, SHUT_RDWR);
                doClose(from);

                // find the child
                for (int j = 0; j < MAX_CLIENT; j++) {
                    if (clients[j].fd == from) {
                        // kill the child
                        kill(clients[j].pid, SIGKILL);
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
void acceptClient(int sockfd) {
    struct sockaddr_in caddr;
    unsigned int clen = sizeof(caddr);
    int clientfd;
    int clientpid;
    memset(&caddr, 0, sizeof(caddr));
    clientfd = accept(sockfd, (struct sockaddr *) &caddr, &clen);
    if (clientfd > 0) {
        LogI(DAEMON, "New client %d", clientfd);
        int clientIdx = addClientToList(clientfd);

        // pre-fork
        clientpid = fork();
        if (clientpid == 0) {
            execShell(clientIdx);
        }
        else {
            // save pid to the list
            clients[clientIdx].pid = clientpid;
        }
    }
}

/**
 * Wait and serve all clients
 */
void serveClients(int sockfd, int errSockfd) {
    fd_set readset;

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

    while (true) {
        int maxfd = addDaemonFdsToReadset(sockfd, &readset);

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
                disconnectDeadClients();
            }
        }
        else if (selectVal == 0) {
            LogI(DAEMON, "Nothing for daemon select()");
        }
        else if (selectVal > 0) {
            // is that an incoming connection from the listening socket?
            if (FD_ISSET(sockfd, &readset)) {
                acceptClient(sockfd);
            }
            // is that data from children or clients?
            daemonForward(&readset);
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
    int sockfd = initSocket(TINYSU_PORT);
    memset(clients, 0, sizeof(clients));
    memset(errSockfds, 0, sizeof(errSockfds));
    serveClients(sockfd, 0);
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

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

void printUsage(char *self) {
    fprintf(stderr, "Usage: %s [-h] [-d] [-c command]\n", self);
    exit(1);
}

void goDaemonMode() {
    printf("I'm going to daemon mode.\n");
    exit(0);
}

int main(int argc, char **argv) {
    int opt = 0;
    printf("This is TinySU.\n");
    while ((opt = getopt(argc, argv, "hd")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                break;
            case 'd':
                goDaemonMode();
                break;
            default: /* '?' */
                printUsage(argv[0]);
        }
    }
    printUsage(argv[0]);
}

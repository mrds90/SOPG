#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>

#define FIFO_NAME "myfifo"
#define BUFFER_SIZE 300
#define HEADER_SIZE 5

static void RecibiSignal(int signum) ;

static char outputBuffer[BUFFER_SIZE];

int main(void) {
    static char inputBuffer[BUFFER_SIZE - HEADER_SIZE];
    struct sigaction sa1;
    struct sigaction sa2;

    sa1.sa_handler = RecibiSignal;
    sa1.sa_flags = 0; //SA_RESTART;
    sigemptyset(&sa1.sa_mask);
    sigaction(SIGUSR1, &sa1, NULL);

    sa2.sa_handler = RecibiSignal;
    sa2.sa_flags = 0; //SA_RESTART;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGUSR2, &sa2, NULL);

    uint32_t bytesWrote;
    int32_t returnCode, fd;

    /* Create named fifo. -1 means already exists so no action if already exists */
    if ((returnCode = mknod(FIFO_NAME, S_IFIFO | 0666, 0)) < -1) {
        printf("Error creating named fifo: %d\n", returnCode);
        exit(1);
    }

    /* Open named fifo. Blocks until other process opens it */
    printf("waiting for readers...\n");
    if ((fd = open(FIFO_NAME, O_WRONLY)) < 0) {
        printf("Error opening named fifo file: %d\n", fd);
        exit(1);
    }

    /* open syscalls returned without error -> other process attached to named fifo */
    printf("got a reader--type some stuff\n");

    /* Loop forever */
    while (1) {
        /* Get some text from console */

        if (fgets(inputBuffer, BUFFER_SIZE, stdin) != NULL) {
            sprintf(outputBuffer, "DATA:%s", inputBuffer);
        }

        if ((bytesWrote = write(fd, outputBuffer, strlen(outputBuffer) - 1)) == -1) {
            perror("write");
        }
        else {
            printf("writer: wrote %d bytes\n", bytesWrote);
        }
    }
    return 0;
}

static void RecibiSignal(int signum) {
    if (signum == SIGUSR1) {
        sprintf(outputBuffer, "SIGN:1\n");
    }
    else if (signum == SIGUSR2) {
        sprintf(outputBuffer, "SIGN:2\n");
    }
}

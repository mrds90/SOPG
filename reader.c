#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

#define FIFO_NAME "myfifo"
#define SIGN_FILE "Sing.txt"
#define LOG_FILE  "Log.txt"
#define BUFFER_SIZE 300
#define HEADER_SIZE 6

int main(void) {
    char input_buffer[BUFFER_SIZE];
    char header[HEADER_SIZE];
    int32_t bytes_read, return_code, fd;
    FILE *fd_sign = fopen(SIGN_FILE, "w");
    FILE *fd_log = fopen(LOG_FILE, "w");

    /* Create named fifo. -1 means already exists so no action if already exists */
    if ((return_code = mknod(FIFO_NAME, S_IFIFO | 0666, 0)) < -1) {
        printf("Error creating named fifo: %d\n", return_code);
        exit(1);
    }
    /* Open named fifo. Blocks until other process opens it */
    printf("waiting for writers...\n");
    if ((fd = open(FIFO_NAME, O_RDONLY)) < 0) {
        printf("Error opening named fifo file: %d\n", fd);
        exit(1);
    }

    printf("got a writer\n");

    do {
        /* read data into local buffer */
        if ((bytes_read = read(fd, input_buffer, BUFFER_SIZE)) == -1) {
            perror("read");
        }
        else {
            input_buffer[bytes_read] = '\0';

            printf("reader: read %d bytes: \"%s\"\n", bytes_read, input_buffer);

            snprintf(header, HEADER_SIZE, "%s", input_buffer);
            if (strcmp(header, "SIGN:") == 0) {
                fprintf(fd_sign, "%s\n", &input_buffer[HEADER_SIZE - 1]);
                printf("Sign.txt updated with \"%s\"\n", &input_buffer[HEADER_SIZE - 1]);
            }
            else if (strcmp(header, "DATA:") == 0) {
                fprintf(fd_log, "%s\n", &input_buffer[HEADER_SIZE - 1]);
                printf("Log.txt updated with \"%s\"\n", &input_buffer[HEADER_SIZE - 1]);
            }
        }
    }
    while (bytes_read > 0);
    fclose(fd_sign);
    fclose(fd_log);

    return 0;
}

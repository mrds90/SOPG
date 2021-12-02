#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

#include "SerialManager.h"

#define BUFFER_CIAA_DOWN              ">OUTS:X,Y,W,Z\r\n"
#define BUFFER_SIZE_CIAA_DOWN         sizeof(BUFFER_CIAA_DOWN)
#define BUFFER_CIAA_DOWN_FORMAT       ">OUTS:%.1d,%.1d,%.1d,%.1d\r\n"
#define BUFFER_CIAA_UP                ">TOGGLE STATE:X\r\n"
#define BUFFER_CIAA_UP_INIT           ">TOGGLE STATE:"
#define BUFFER_SIZE_CIAA_UP           sizeof(BUFFER_CIAA_UP)

#define BUFFER_INTERFASE_UP           ":LINE0TG\n"
#define BUFFER_SIZE_INTERFASE_UP      sizeof(BUFFER_INTERFASE_UP)
#define BUFFER_INTERFASE_UP_FORMAT    ":LINE%dTG\n"
#define BUFFER_FORMAT_INTERFASE_DOWN  ":STATESXYWZ\n"
#define BUFFER_SIZE_INTERFASE_DOWN    sizeof(BUFFER_FORMAT_INTERFASE_DOWN)
#define BUFFER_INTERFASE_DOWN         ":STATES%.1d%.1d%.1d%.1d\n"

#define PORT_SERVER                    "127.0.0.1"

#define BAUD_RATE_CIAA             115200
#define SINGAL_ERROR               -1
#define ERROR                      -1

#define TRUE                       1
#define FALSE                      0

#define CHAR_TO_INT(c)             ((int)c - '0')
#define INT_TO_CHAR(i)             ((char)(i + '0'))
typedef enum {
    LINE_A,
    LINE_B,
    LINE_C,
    LINE_D,

    LINE_QTY,
} line_t;

typedef enum {
    STATE_ON,
    STATE_OFF,
    STATE_TOGGLE,

    STATE_QTY,
} state_t;
//======[Thread Functions Declarations]========================================
/**
 * @brief Thread to read the serial port and send the data to the CIAA
 *
 * @param arg
 */
void*SerialManager(void *arg);

/**
 * @brief Create the thread to manage the socket TCP
 *
 * @param arg
 * @return void*
 */
void*InterfaceManager(void *arg);

/**
 * @brief Send the data to the socket TCP
 *
 * @param arg
 * @return void*
 */
void*InterfaceManagerSend(void *arg);

//======[Private Functions Declarations]=======================================
/**
 * @brief Handle the SIGINT and SIGTERM signals to end the program properly.
 *
 * @param signum
 */
static void SignalHandler(int signum);

/**
 * @brief Block the signals
 *
 */
static void BlockSignals(void);

/**
 * @brief Unlock the signals handlers
 *
 */
void UnblockSignals(void);


static int toggle_line = 0;
static int socket_fd = 0;
static uint8_t toggle_flag = FALSE;
static int socket_descriptor;

int main(void) {
    struct sigaction sa1;
    struct sigaction sa2;
    state_t lines[LINE_QTY];
    sa1.sa_handler = SignalHandler;
    sa1.sa_flags = 0;
    sigemptyset(&sa1.sa_mask);
    if (sigaction(SIGTERM, &sa2, NULL) == SINGAL_ERROR) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    sa2.sa_handler = SignalHandler;
    sa2.sa_flags = 0;
    sigemptyset(&sa2.sa_mask);
    if (sigaction(SIGINT, &sa2, NULL) == SINGAL_ERROR) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    printf("Inicio Serial Service\r\n");
    BlockSignals();
    pthread_t thread_serial;
    int ret = pthread_create(&thread_serial, NULL, SerialManager, &lines);
    if (ret != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    pthread_t thread_interface;
    ret = pthread_create(&thread_interface, NULL, InterfaceManager, &lines);
    if (ret != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    UnblockSignals();
    while (1) {
        sleep(1);
    }


    exit(EXIT_SUCCESS);
    return 0;
}

//======[Thread Functions Implementation]========================================
void*SerialManager(void *arg) {
    state_t *lines = (state_t *) arg;
    char buffer_ciaa_down[BUFFER_SIZE_CIAA_DOWN];
    if (!serial_open(1, BAUD_RATE_CIAA)) {
        while (1) {
            snprintf(buffer_ciaa_down, BUFFER_SIZE_CIAA_DOWN, BUFFER_CIAA_DOWN_FORMAT, lines[LINE_A], lines[LINE_B], lines[LINE_C], lines[LINE_D]);
            serial_send(buffer_ciaa_down, BUFFER_SIZE_CIAA_DOWN + 1);
            usleep(200000);
            char buffer_ciaa_up[BUFFER_SIZE_CIAA_UP];
            memset(buffer_ciaa_up, 0, BUFFER_SIZE_CIAA_UP + 1);
            if (serial_receive(buffer_ciaa_up, BUFFER_SIZE_CIAA_UP) > 0) {
                printf("%s", buffer_ciaa_up);
                if (strlen(buffer_ciaa_up) >= sizeof(BUFFER_CIAA_UP_INIT)) {
                    toggle_line = CHAR_TO_INT(buffer_ciaa_up[14]);
                    printf("Toggle line: %d\r\n", toggle_line);
                    toggle_flag = TRUE;
                }
            }

            usleep(200000);
        }
    }
    return NULL;
}

void*InterfaceManager(void *arg) {
    state_t *lines = (state_t *) arg;
    socklen_t address_length;
    
    struct sockaddr_in address_client, address_server;
    char data_down[BUFFER_SIZE_INTERFASE_DOWN];


    socket_fd = socket(AF_INET, SOCK_STREAM, 0); // Create the socket
    if (socket_fd == ERROR) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    bzero((char *) &address_server, sizeof(address_server));
    address_server.sin_family = AF_INET;
    address_server.sin_port = htons(10000);
    address_server.sin_addr.s_addr = inet_addr(PORT_SERVER);

    if (address_server.sin_addr.s_addr == INADDR_NONE) {
        perror("inet_addr");
        exit(EXIT_FAILURE);
    }

    if (bind(socket_fd, (struct sockaddr *)&address_server, sizeof(address_server)) == ERROR) {
        close(socket_fd);
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if (listen(socket_fd, 2 * LINE_QTY) == ERROR) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    pthread_t thread_interface;
    int ret = pthread_create(&thread_interface, NULL, InterfaceManagerSend, NULL);
    if (ret != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    while (1) {
        address_length = sizeof(struct sockaddr_in);
        if ((socket_descriptor = accept(socket_fd, (struct sockaddr *)&address_client, &address_length)) == ERROR) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        printf("server connected from  %s\n", inet_ntoa(address_client.sin_addr));

        int read_bytes;
        while (TRUE) {
            if ((read_bytes = read(socket_descriptor, data_down, 128)) == ERROR) {
                perror("read");
                exit(EXIT_FAILURE);
            }
            data_down[read_bytes] = 0;
            printf("%d bytes received with %s\n", read_bytes, data_down);
            //TODO: Critical section:
            lines[LINE_A] = CHAR_TO_INT(data_down[7]);
            lines[LINE_B] = CHAR_TO_INT(data_down[8]);
            lines[LINE_C] = CHAR_TO_INT(data_down[9]);
            lines[LINE_D] = CHAR_TO_INT(data_down[10]);
            //TODO: End critical section
        }

        close(socket_fd);
        usleep(200000);
    }
}

void*InterfaceManagerSend(void *arg) {
    char data_up [BUFFER_SIZE_INTERFASE_UP] = BUFFER_INTERFASE_UP;
    while (TRUE) {
        data_up[5] = INT_TO_CHAR(toggle_line);
        if (toggle_flag == TRUE) {
            toggle_flag = FALSE;
            if (write(socket_descriptor, data_up, BUFFER_SIZE_INTERFASE_UP) == ERROR) {
                perror("write");
                exit(EXIT_FAILURE);
            }
        }
        usleep(200000);
    }
}

//======[Private Functions Implementation]=====================================
static void SignalHandler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        printf("\n\rSignal %d received\n\r", signum);
        serial_close();
        close(socket_fd);
        close(socket_descriptor);
        exit(EXIT_SUCCESS);
    }
}

static void BlockSignals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void UnblockSignals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

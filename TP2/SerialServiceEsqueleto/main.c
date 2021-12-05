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

#define LINE_TOGLE_POSITION           14
#define LINE_INFO_POSITION            7
#define BAUD_RATE_CIAA                115200
#define SINGAL_ERROR                  -1
#define ERROR                         -1

#define TRUE                          1
#define FALSE                         0

#define CHAR_TO_INT(c)                ((int)c - '0')
#define INT_TO_CHAR(i)                ((char)(i + '0'))
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
static uint8_t socket_descriptor_flag = FALSE;
static int socket_descriptor;
static pthread_t thread_interface;
volatile sig_atomic_t end_system = FALSE;
static pthread_mutex_t mutexData = PTHREAD_MUTEX_INITIALIZER;

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


    struct sockaddr_in address_server;

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

    BlockSignals();

    int ret = pthread_create(&thread_interface, NULL, InterfaceManager, &lines);
    if (ret != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    UnblockSignals();


    char buffer_ciaa_down[BUFFER_SIZE_CIAA_DOWN];
    if (!serial_open(1, BAUD_RATE_CIAA)) {
        while (1) {
            pthread_mutex_lock(&mutexData);
            snprintf(buffer_ciaa_down, BUFFER_SIZE_CIAA_DOWN, BUFFER_CIAA_DOWN_FORMAT, lines[LINE_A], lines[LINE_B], lines[LINE_C], lines[LINE_D]);
            pthread_mutex_unlock(&mutexData);

            serial_send(buffer_ciaa_down, BUFFER_SIZE_CIAA_DOWN + 1);

            usleep(10000);

            char buffer_ciaa_up[BUFFER_SIZE_CIAA_UP];
            memset(buffer_ciaa_up, 0, BUFFER_SIZE_CIAA_UP + 1);
            if (serial_receive(buffer_ciaa_up, BUFFER_SIZE_CIAA_UP) > 0) {
                printf("%s", buffer_ciaa_up);
                if (strlen(buffer_ciaa_up) >= sizeof(BUFFER_CIAA_UP_INIT)) {
                    toggle_line = CHAR_TO_INT(buffer_ciaa_up[LINE_TOGLE_POSITION]);
                    printf("Toggle line: %d\r\n", toggle_line);
                    toggle_flag = TRUE;
                }
            }

            char data_up [BUFFER_SIZE_INTERFASE_UP] = BUFFER_INTERFASE_UP;
            data_up[5] = INT_TO_CHAR(toggle_line);
            if (toggle_flag == TRUE) {
                toggle_flag = FALSE;

                pthread_mutex_lock(&mutexData);
                if (socket_descriptor_flag) {
                    if (write(socket_descriptor, data_up, BUFFER_SIZE_INTERFASE_UP) == ERROR) {
                        perror("write");
                        exit(EXIT_FAILURE);
                    }
                }
                pthread_mutex_unlock(&mutexData);
            }
            if(end_system == TRUE) {
                break;
            }
        }
    }

    
    if(end_system == TRUE) {   
        void *thread_cancel;
        printf("%s", "Close all the resources\r\n");
        pthread_cancel(thread_interface);
        pthread_join(thread_interface, &thread_cancel);
	    if(thread_cancel == PTHREAD_CANCELED) {
            printf("%s", "Thread was canceled\r\n");
        }
        else {
            printf("%s", "Thread ended\r\n");
        }
        serial_close();
        close(socket_fd);
        close(socket_descriptor);
        exit(EXIT_SUCCESS);
    }

    return 0;
}

//======[Thread Functions Implementation]========================================

void*InterfaceManager(void *arg) {
    state_t *lines = (state_t *) arg;
    struct sockaddr_in address_client;
    char data_down[BUFFER_SIZE_INTERFASE_DOWN];
    socklen_t address_length;

    while (1) {
        address_length = sizeof(struct sockaddr_in);
        if ((socket_descriptor = accept(socket_fd, (struct sockaddr *)&address_client, &address_length)) == ERROR) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        else {
            pthread_mutex_lock(&mutexData);
            socket_descriptor_flag = TRUE;
            pthread_mutex_unlock(&mutexData);
        }
        printf("server connected from  %s\n", inet_ntoa(address_client.sin_addr));

        int read_bytes;
        while (((read_bytes = read(socket_descriptor, data_down, 128)) != ERROR) && (read_bytes > 0)) {
            data_down[read_bytes] = 0;
            printf("%d bytes received with %s\n", read_bytes, data_down);
            pthread_mutex_lock(&mutexData);
            lines[LINE_A] = CHAR_TO_INT(data_down[LINE_INFO_POSITION + LINE_A]);
            lines[LINE_B] = CHAR_TO_INT(data_down[LINE_INFO_POSITION + LINE_B]);
            lines[LINE_C] = CHAR_TO_INT(data_down[LINE_INFO_POSITION + LINE_C]);
            lines[LINE_D] = CHAR_TO_INT(data_down[LINE_INFO_POSITION + LINE_D]);
            pthread_mutex_unlock(&mutexData);
        }

        pthread_mutex_lock(&mutexData);
        socket_descriptor_flag = FALSE;
        pthread_mutex_unlock(&mutexData);

        close(socket_descriptor);
    }
}

//======[Private Functions Implementation]=====================================

static void SignalHandler(int signum) {
    if (signum == SIGTERM || signum == SIGINT) {
        printf("\n\rSignal %d received\n\r", signum);
        end_system = TRUE;
    }
}

static void BlockSignals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, NULL);
}

void UnblockSignals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);
}

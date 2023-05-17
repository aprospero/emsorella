#define _GNU_SOURCE 1

#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "serial.h"
#include "defines.h"
#include "queue.h"
#include "rx.h"
#include "tool/logger.h"


#define handle_error_en(en, msg) do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

struct termios tios;
int waitingfor;
struct STATS stats;
pthread_t readloop = 0;
int logging = 0;

void print_stats() {
    LOG_INFO("Statistics");
    LOG_INFO("RX bus access errors    %d", stats.rx_mac_errors);
    LOG_INFO("RX total                %d", stats.rx_total);
    LOG_INFO("RX success              %d", stats.rx_success);
    LOG_INFO("RX too short            %d", stats.rx_short);
    LOG_INFO("RX wrong sender         %d", stats.rx_sender);
    LOG_INFO("RX CRC errors           %d", stats.rx_format);
    LOG_INFO("TX total                %d", stats.tx_total);
    LOG_INFO("TX failures             %d", stats.tx_fail);
}

void print_packet(int out, enum log_level loglevel, const char * prefix, uint8_t *msg, size_t len) {
    if (!log_get_level(loglevel))
        return;
    char text[3 + MAX_PACKET_SIZE * 3 + 2 + 1];
    int pos = 0;
    pos += sprintf(&text[0], "%s (%d) %cX:", prefix, len, out ? 'T' : 'R');

    for (size_t i = 0; i < len; i++) {
        pos += sprintf(&text[pos], " %02hhx", msg[i]);
        if (i == 3 || i == len - 2) {
            pos += sprintf(&text[pos], " ");
        }
    }
    log_push(loglevel, "%s", text);
}

void stop_handler() {
    close_queues();
    close_serial();
    readloop = 0;
}

void *read_loop() {
    int abort;

    {
        // Do not accept signals. They should be handled by calling code.
        // We close cleanly when we're asked to stop with pthread_cancel.
        sigset_t set;
        int ret;
        sigemptyset(&set);
        sigaddset(&set, SIGQUIT);
        ret = pthread_sigmask(SIG_BLOCK, &set, NULL);
        if (ret != 0)
            handle_error_en(ret, "pthread_sigmask");
    }

    pthread_cleanup_push(stop_handler, NULL);
    LOG_INFO("Starting EMS bus access");
    while (1) {
        rx_packet(&abort);
        rx_done();
    }
    pthread_cleanup_pop(1);
    return NULL;
}

int start(char *port_path) {
    int ret;

    ret = open_serial(port_path);
    if (ret != 0) {
        LOG_ERROR("Failed to open %s: %i", port_path, ret);
        return(-1);
    }
    LOG_INFO("Serial port %s opened", port_path);

    setup_queue(&tx_queue, TX_QUEUE_NAME);
    if (tx_queue == -1) {
        LOG_ERROR("Failed to open TX message queue: %i %s", tx_queue, strerror(errno));
        return(-1);
    }
    setup_queue(&rx_queue, RX_QUEUE_NAME);
    if (rx_queue == -1) {
        LOG_ERROR("Failed to open RX message queue: %i  %s", rx_queue, strerror(errno));
        return(-1);
    }
    LOG_INFO("Connected to message queues");

    ret = pthread_create(&readloop, NULL, &read_loop, NULL);
    if (ret != 0)
        handle_error_en(ret, "pthread_create");

    return(0);
}

int stop() {
    if (!readloop)
        return(-1);
    pthread_cancel(readloop);
    return(0);
}

void sig_stop() {
    stop();
}

int main(int argc, char *argv[]) {
    int ret;
    struct sigaction signal_action;

    log_init("ems_serio",  LF_STDOUT, LL_INFO);

    if (argc < 3) {
      LOG_ERROR("Usage: %s <ttypath> <logmask>\n", argv[0]);
        return(-1);
    }

//    log_set_level(atoi(argv[2]), TRUE);
    ret = start(argv[1]);

    // Set signal handler and wait for the thread
    signal_action.sa_handler = sig_stop;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGHUP, &signal_action, NULL);
    sigaction(SIGTERM, &signal_action, NULL);

    pthread_join(readloop, NULL);
    print_stats();

    return(ret);
}

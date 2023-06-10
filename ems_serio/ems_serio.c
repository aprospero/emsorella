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
#include <signal.h>

#include "serial.h"
#include "defines.h"
#include "rx.h"
#include "ctrl/com/mqtt.h"
#include "tool/logger.h"


struct STATS stats;
struct mqtt_handle * mqtt;
int abort_rx_loop = FALSE;

void print_stats() {
    LG_INFO("Statistics");
    LG_INFO("RX bus access errors    %d", stats.rx_mac_errors);
    LG_INFO("RX total                %d", stats.rx_total);
    LG_INFO("RX success              %d", stats.rx_success);
    LG_INFO("RX too short            %d", stats.rx_short);
    LG_INFO("RX wrong sender         %d", stats.rx_sender);
    LG_INFO("RX CRC errors           %d", stats.rx_format);
    LG_INFO("TX total                %d", stats.tx_total);
    LG_INFO("TX failures             %d", stats.tx_fail);
}

int read_loop(const char * serial_port)
{
  int ret = open_serial(serial_port);
  if (ret != 0)
  {
      LG_CRITICAL("Failed to open %s: %i", serial_port, ret);
      goto FAILURE;
  }
  LG_INFO("Serial port %s opened", serial_port);

  LG_INFO("Initializing MQTT API.");
  mqtt = mqtt_init("ems", "MTDC", 2);
  if (mqtt == NULL)
  {
    LG_CRITICAL("Could not initialize mqtt API.");
    goto FAILURE;
  }
  LG_INFO("MQTT API Initialized.\n", serial_port);

    LG_INFO("Starting EMS bus access.");
    while (abort_rx_loop == FALSE) {
        rx_packet(&abort_rx_loop);
        if (abort_rx_loop == FALSE)
          rx_done();
        if (abort_rx_loop == FALSE)
          mqtt_loop(mqtt, 0);
    }
    mqtt_close(mqtt);
    close_serial();
    print_stats();
    return 0;

FAILURE:
    if (ret)
      close_serial();
    if (mqtt)
      mqtt_close(mqtt);
    return -1;
}

void sig_stop() {
  abort_rx_loop = TRUE;
}

int main(int argc, char *argv[]) {

   struct sigaction signal_action;

    log_init("ems_serio",  LF_STDOUT, LL_INFO);

    if (argc < 2) {
      LG_ERROR("Usage: %s <ttypath> [logmask:default=error]\n", argv[0]);
        return(-1);
    }
    if (argc == 3)
      log_set_level_state(atoi(argv[2]), TRUE);

    // Set signal handler and wait for the thread
    signal_action.sa_handler = sig_stop;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGHUP, &signal_action, NULL);
    sigaction(SIGTERM, &signal_action, NULL);

    return read_loop(argv[1]);
}

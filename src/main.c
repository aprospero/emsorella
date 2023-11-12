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

#include "io/serial.h"
#include "defines.h"
#include "io/rx.h"
#include "io/tx.h"
#include "ctrl/com/mqtt.h"
#include "ctrl/logger.h"
#include "tools/msg_queue.h"
#include "ctrl/com/ems.h"
#include "tools/stats.h"
#include "args.h"
#include "version.h"

#define DEFAULT_LOG_FAC LF_LOCAL1

uint8_t tx_buf[1024];

struct emsorella_config cfg;

int abort_rx_loop = FALSE;

int read_loop()
{
  struct mqtt_handle * mqtt = NULL;
  int ret;

  mq_init(tx_buf, sizeof(tx_buf));

  ret = serial_open(cfg.serial_device);
  if (ret != 0)
  {
    LG_CRITICAL("Failed to open %s: %i", cfg.serial_device, ret);
    goto END;
  }
  LG_INFO("Serial port %s opened", cfg.serial_device);

  LG_INFO("Initializing MQTT API.");
  mqtt = mqtt_init(&cfg.mqtt);
  if (mqtt == NULL)
  {
    LG_CRITICAL("Could not initialize mqtt API.");
    goto END;
  }
  ems_init(mqtt);
  LG_INFO("MQTT API Initialized.\n");

  LG_INFO("Starting EMS bus access.");

  for(int phase = 0; !abort_rx_loop; phase ^= 0x01)
    switch (phase) {
      default: break;

      case 0: if (rx_packet(&abort_rx_loop))
                tx_update();
              break;
      case 1: mqtt_loop(mqtt, 0);
              break;
    }

  print_stats();

END:
  if (!ret)
    serial_close();
  if (mqtt) {
    mqtt_close(mqtt);
    return 0;
  }
  return -1;
}

void sig_stop() {
  abort_rx_loop = TRUE;
}

int main(int argc, char *argv[]) {

   struct sigaction signal_action;

   parseArgs(argc, argv, &cfg);

   char * app_name = strrchr(argv[0], '/');
   if (cfg.prg_name == NULL)
     app_name = argv[0];
   else
     app_name++;

    log_init("ems_serio",  DEFAULT_LOG_FAC, LL_CRITICAL);

    if (argc < 2) {
      LG_ERROR("Usage: %s <ttypath> [logmask:default=error]\n", argv[0]);
        return(-1);
    }
    if (argc == 3)
      log_set_level_state(atoi(argv[2]), TRUE);

    log_push(LL_NONE, "############################################################################################");
    log_push(LL_NONE, "Starting %s "APP_VERSION" - on:%s, LogFacility:%s Level:%s.",
             cfg.prg_name, cfg.serial_device, log_get_facility_name(cfg.log_facility), log_get_level_name(cfg.log_level, TRUE));
    log_push(LL_NONE, "############################################################################################");
    // Set signal handler
    signal_action.sa_handler = sig_stop;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGHUP, &signal_action, NULL);
    sigaction(SIGTERM, &signal_action, NULL);

    return read_loop();
}

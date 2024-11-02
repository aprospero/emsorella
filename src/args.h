#ifndef _H_ARGS
#define _H_ARGS

#include "ctrl/com/mqtt.h"
#include "ctrl/logger.h"

#ifndef EMSORELLA_TEST
#define DEFAULT_LOG_FACILITY LF_LOCAL1
#define DEFAULT_LOG_LEVEL    LL_ERROR
#else
#define DEFAULT_LOG_FACILITY LF_STDOUT
#define DEFAULT_LOG_LEVEL    LL_DEBUG
#endif

#define DEFAULT_SERIAL_DEVICE "/dev/ttymxc0"

#define DEFAULT_MQTT_REMOTE "localhost"
#define DEFAULT_MQTT_PORT   1883

#define DEFAULT_MQTT_CLIENT_ID "ems"
#define DEFAULT_MQTT_TOPIC     "MTDC"
#define DEFAULT_MQTT_QOS       2


struct emsorella_config
{
    const char *       prg_name;
    enum log_facility  log_facility;
    enum log_level     log_level;
    char *             serial_device;
    struct mqtt_config mqtt;
};

int parseArgs(int argc, char * argv[], struct emsorella_config * config);


#endif

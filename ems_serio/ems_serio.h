#include <pthread.h>

#include "defines.h"
#include "tool/logger.h"

extern struct STATS stats;
extern struct mqtt_handle * mqtt;
extern pthread_t readloop;

int start(char *);
int stop();

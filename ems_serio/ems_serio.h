#include <pthread.h>

#include "defines.h"
#include "tool/logger.h"

extern struct STATS stats;
extern int logging;
extern pthread_t readloop;

int start(char *);
int stop();
void print_packet(int out, enum log_level loglevel, const char * prefix, uint8_t *msg, size_t len);

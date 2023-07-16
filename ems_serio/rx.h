#ifndef __RX_H
#define __RX_H

#include <stdint.h>
#include <sys/time.h>

#include "defines.h"


extern uint8_t read_expected[HDR_LEN];
extern struct timeval got_bus;

void rx_packet(int *abort);
void rx_done();

#endif // __RX_H

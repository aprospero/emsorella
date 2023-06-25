#ifndef __TX_H
#define __TX_H

#include "defines.h"

extern uint8_t tx_buf[MAX_PACKET_SIZE];
extern size_t tx_len;


void handle_poll();

#endif // __TX_H

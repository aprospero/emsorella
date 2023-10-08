#ifndef __DEFINES_H
#define __DEFINES_H

#define MAX_PACKET_SIZE 64
#define SRCPOS 0
#define DSTPOS 1
#define HDR_LEN 4UL

#define BROADCAST_ID 0x00
#define MASTER_ID 0x08
#define CLIENT_ID 0x0b

#define MAX_TX_RETRIES 5
#define ACK_LEN 1
#define ACK_VALUE 0x01
#define MAX_BUS_TIME 200  /* ms */

#endif

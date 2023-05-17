#define MAX_PACKET_SIZE 64
#define SRCPOS 0
#define DSTPOS 1
#define HDR_LEN 4

#define BROADCAST_ID 0x00
#define MASTER_ID 0x08
#define CLIENT_ID 0x0b

#define RX_QUEUE_NAME "/ems_bus_rx"
#define TX_QUEUE_NAME "/ems_bus_tx"

#define BREAK_IN "\xFF\x00\x00"
#define BREAK_OUT "\x00"
#define MAX_TX_RETRIES 5
#define ACK_LEN 1
#define ACK_VALUE 0x01
#define MAX_BUS_TIME 200 * 1000

struct STATS {
    unsigned int rx_mac_errors;
    unsigned int rx_total;
    unsigned int rx_success;
    unsigned int rx_short;
    unsigned int rx_sender; // Bad senders
    unsigned int rx_format; // Bad format
    unsigned int rx_crc;
    unsigned int tx_total;
    unsigned int tx_fail;
};

enum STATE { RELEASED, ASSIGNED, WROTE, READ };

#ifndef _H_TOOL_STATS
#define _H_TOOL_STATS

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

extern struct STATS stats;


void print_stats(void);


#endif  // _H_TOOL_STATS

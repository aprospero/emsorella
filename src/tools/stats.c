#include "stats.h"
#include "linuxtools/ctrl/logger.h"

struct STATS stats;

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


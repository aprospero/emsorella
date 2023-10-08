#ifndef _H_TOOL_CRC
#define _H_TOOL_CRC

#include <stdint.h>
#include <unistd.h>

uint8_t calc_crc(uint8_t *data, ssize_t len);
#endif  // _H_TOOL_CRC

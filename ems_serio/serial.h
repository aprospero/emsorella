#ifndef __SERIAL_H_
#define __SERIAL_H_

#define SERIAL_RX_BREAK 0x7FFFFFFFL

int serial_open(const char *);
int serial_close();

int serial_wait();  /* waits up to 200ms or until a character is readable */

int serial_pop_byte(uint8_t * buf);  /* returns  SERIAL_RX_BREAK on a received BREAK */
int serial_push_byte(uint8_t byte);

void serial_send_break();

#endif   // __SERIAL_H_

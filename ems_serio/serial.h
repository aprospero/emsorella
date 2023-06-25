#ifndef __SERIAL_H_
#define __SERIAL_H_


extern int serial_open(const char *);
extern int serial_close();

void serial_send_break();

extern int port;


#endif   // __SERIAL_H_

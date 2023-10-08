#ifndef __CTRL_COM_STATE_H
#define __CTRL_COM_STATE_H

#include <stdint.h>

#include "defines.h"

enum STATE {
  RELEASED = 0x00,
  ASSIGNED,
  WROTE,
  READ
};

// sets content of expected ems-msg answer. Input the msg we expect an answer for.
void state_set_expected(uint8_t * data);
// compares msg with expected answer
int state_cmp_expected(uint8_t * data);

// sets time when bus is aquired
void state_get_bus(void);
// returns time we got bus in ms
int state_got_bus(void);

void state_set(enum STATE);

int state_is(enum STATE);

enum STATE state_get(void);

const char * state_get_str(void);


#endif // __CTRL_COM_STATE_H

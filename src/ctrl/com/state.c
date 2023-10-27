#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <string.h>

#include "state.h"
#include "stuff.h"

enum STATE int_state = RELEASED;
const char * state_str[] =
{
  "RELEASED",
  "ASSIGNED",
  "WROTE",
  "READ"
};



uint8_t read_expected[HDR_LEN];

struct timeval got_bus;

// sets content of expected ems-msg answer.
void state_set_expected(uint8_t * data)
{
  read_expected[0] = data[1] & 0x7f;
  read_expected[1] = data[0];
  read_expected[2] = data[2];
  read_expected[3] = data[3];
}
// compares msg with expected answer
int state_cmp_expected(uint8_t * data)
{
  return memcmp(data, &read_expected, HDR_LEN);
}

// sets time when bus is aquired
void state_get_bus(void)
{
  gettimeofday(&got_bus, NULL);
}
// returns time we got bus in ms
int state_got_bus(void)
{
  struct timeval now;
  int32_t have_bus;
  if (got_bus.tv_sec == 0 && got_bus.tv_usec == 0)
    return FALSE;
  gettimeofday(&now, NULL);
  have_bus = ((now.tv_sec - got_bus.tv_sec) * 1000) + ((now.tv_usec - got_bus.tv_usec) / 1000);
  return have_bus < MAX_BUS_TIME ? TRUE : FALSE;
}

void state_set(enum STATE new_state)
{
  int_state = new_state;
}

int state_is(enum STATE is_state)
{
  return int_state == is_state ? TRUE : FALSE;
}

enum STATE state_get()
{
  return int_state;
}

const char * state_get_str(void)
{
  return state_str[int_state];
}



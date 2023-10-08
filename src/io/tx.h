#ifndef __TX_H
#define __TX_H

#include <sys/time.h>

#include "../defines.h"

extern enum STATE state;


#ifdef __cplusplus
extern "C"
{
#endif

void handle_poll(struct timeval got_bus);

#ifdef __cplusplus
}
#endif


#endif // __TX_H

#ifndef __MSG_QUEUE_H
#define __MSG_QUEUE_H

#include <stdint.h>
#include <stddef.h>

#include "defines.h"

struct mq_message
{
  uint8_t * buf;
  size_t    len;
};

#ifdef __cplusplus
extern "C"
{
#endif

int                 mq_init(uint8_t * buf, size_t len);
int                 mq_push(uint8_t * buf, size_t len, int do_copy);
struct mq_message * mq_peek();
void                mq_pull();
int                 mq_owns_mem(struct mq_message * msg);

#ifdef __cplusplus
}
#endif

#endif // __MSG_QUEUE_H

#ifndef _MSG_QUEUE_H
#define _MSG_QUEUE_H

#include <stdint.h>
#include <stddef.h>

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
uint8_t *           mq_would_fit(size_t len, int do_copy);
int                 mq_owns_mem(struct mq_message * msg);
size_t              mq_get_free();

#ifdef __cplusplus
}
#endif

#endif // _MSG_QUEUE_H

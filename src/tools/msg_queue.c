#include <string.h>

#include "msg_queue.h"
#include "linuxtools/ctrl/logger.h"


#define FREE_HEAD (hnd.buf_sz - (hnd.pfree - hnd.buf))

struct mq_queued_msg
{
  struct mq_queued_msg * next;
  struct mq_message msg;
};

struct mq_handle
{
  struct mq_queued_msg * head;   // head of queue
  struct mq_queued_msg * tail;   // tail of queue
  uint8_t *              buf;    // ringbuffer
  size_t                 buf_sz; // ringbuffer size
  uint8_t *              pfree;  // ptr to free memory in ringbuffer
};

struct mq_handle hnd;


int mq_init(uint8_t * buf, size_t len)
{
  memset(&hnd, 0, sizeof(hnd));
  hnd.buf_sz = len;
  hnd.buf = hnd.pfree = buf;
  return hnd.pfree == NULL ? -1 : 0;
}

int mq_push(uint8_t * buf, size_t len, int do_copy)
{
  struct mq_queued_msg * msg;
  size_t    free_sz;
  uint8_t * palloc = NULL;     // ptr to allocatable memory
  uint8_t * pfree = hnd.pfree; // ptr to start of free memory
  uint8_t * poccupied = hnd.tail == NULL ? hnd.buf + hnd.buf_sz : ((uint8_t *) hnd.head); // ptr to start of occupied memory

  size_t    sz = sizeof(struct mq_queued_msg);  // alculate size of needed memory
  if (do_copy)
    sz += len;

  pfree = hnd.pfree; // find free memory
  poccupied = hnd.head == NULL ? hnd.buf + hnd.buf_sz : ((uint8_t *) hnd.head); // ptr to start of occupied memory
  if (pfree > poccupied)
  {
    free_sz = hnd.buf + hnd.buf_sz - pfree;
    if (free_sz >= sz)
      palloc = pfree;
    free_sz = poccupied - hnd.buf;
    if (free_sz >= sz)
      palloc = hnd.buf;
  }
  else
  {
    free_sz = poccupied - pfree;
    if (free_sz >= sz)
      palloc = pfree;
  }
  if (palloc == NULL)
  {
    LG_ERROR("Out of memory trying to allocate %d Bytes message space.", sz);
    return -1;
  }


  msg = (struct mq_queued_msg *) palloc;  // construct new queued message
  if (do_copy)
  {
    msg->msg.buf = palloc + sizeof(struct mq_queued_msg);
    memcpy(msg->msg.buf, buf, len);
  }
  else
    msg->msg.buf = buf;
  msg->msg.len = len;
  msg->next = NULL;

  if (hnd.tail == NULL)                  // insert new msg at tail of queue
  {
    hnd.tail = hnd.head = msg;
  }
  else
  {
    hnd.tail->next = msg;
    hnd.tail = msg;
  }
  hnd.pfree = palloc + sz;
  return 0;
}


struct mq_message * mq_peek()
{
  return hnd.head == NULL ? NULL : &hnd.head->msg;
}

void mq_pull()
{
  if (hnd.head)
  {
    hnd.head = hnd.head->next;
    if (hnd.head == NULL)
      hnd.tail = NULL;
  }
}

int mq_owns_mem(struct mq_message * msg)
{
  return msg->buf >= hnd.buf && msg->buf < hnd.buf + hnd.buf_sz;
}




#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#include "ctrl/logger.h"
#include "tools/msg_queue.h"
#include "args.h"
#include "version.h"
#include <stringhelp.h>

struct emsorella_config cfg;

#define TEST_Q_BUFSIZE_MIN (sizeof(struct mq_message) + 4)
#define TEST_Q_BUFSIZE_MAX (1024 * 1024 * 4)
#define TEST_Q_LOOP_CNT_PER_Q (256)
#define TEST_Q_LOOP_CNT       (1024 * 1024)

#define TEST_Q_MSGSIZE_MIN 1
#define TEST_Q_MSGSIZE_MAX 64

#define TEST_Q_MSGS_MAX (TEST_Q_BUFSIZE_MAX >> 3)


struct mq_test {
  struct mq_message msgs[TEST_Q_MSGS_MAX];
  size_t read;
  size_t write;
} mq_test;


static inline size_t getRandom(size_t min, size_t max) {
  return min + rand() % (max - min + 1);
}

static int mq_produce_msgs(size_t cnt) {
  for (;cnt > 0; cnt--) {
    size_t    sz      = getRandom(TEST_Q_MSGSIZE_MIN, TEST_Q_MSGSIZE_MAX);
    size_t    do_copy = getRandom(0, 1);
    uint8_t * buf     = NULL;
    size_t    next    = (mq_test.write + 1) % TEST_Q_MSGS_MAX;

    if (!mq_would_fit(sz, do_copy))
      return 0;

    if (next == mq_test.read) {
      LG_ERROR("Can't produce new message - all available msgs in use.");
      return -1;
    }

    buf = calloc(1, sz);
    if (buf == NULL) {
      LG_ERROR("Out of Memory. Could not allocate Msg buffer of size %u.", sz);
      return -1;
    }

    for (size_t u = 0; u < sz; u++)
      buf[u] = getRandom(0, 255);

    if (mq_push(buf, sz, do_copy) != 0) {
      LG_WARN("Could not push new Message to Queue.");
      mq_would_fit(sz, do_copy);
      free(buf);
      return 0;
    }

    mq_test.msgs[mq_test.write].buf = buf;
    mq_test.msgs[mq_test.write].len = sz;
    mq_test.write = next;

  }
  return 0;
}

static int mq_consume_msgs(size_t cnt) {
  for (; cnt > 0; cnt--) {
    size_t              failed = FALSE;
    struct mq_message * cmp    = NULL;
    struct mq_message * msg    = mq_peek();
    size_t              next   = (mq_test.read + 1) % TEST_Q_MSGS_MAX;

    if (msg == NULL)
      return 0;

    if (msg->buf == NULL) {
      LG_CRITICAL("Msg from queue has no allocated buffer.");
      failed = TRUE;
    }
    if (mq_test.read == mq_test.write) {
      LG_CRITICAL("Unexpected msg from Queue. Len: %u.", msg->len);
      failed = TRUE;
    } else {
      cmp = &mq_test.msgs[mq_test.read];
      if (msg->len != cmp->len) {
        LG_CRITICAL("Msg buf from Queue has unexpected length: %u should be %u.", msg->len, cmp->len);
        failed = TRUE;
      }
      if (cmp->buf == NULL) {
        LG_CRITICAL("Expected message has no allocated buffer.");
        failed = TRUE;
      }
    }

    if (failed) {
      return -1;
    }

    if (msg->buf != cmp->buf) {
      for (size_t i = 0; i < msg->len; i++) {
        if (msg->buf[i] != cmp->buf[i]) {
          LG_CRITICAL("Msg from queue has unexpected content at pos %u: 0x%02X should be 0x%02X.", i, msg->buf[i], cmp->buf[i]);
          return -1;
        }
      }
    }
    mq_pull();
    free(cmp->buf);
    cmp->len = 0;
    cmp->buf = NULL;
    mq_test.read = next;
  }
  return 0;
}


#define B2MB (1.0f / (1024.0f * 1024.0f))


int loop()
{
  srand(time(NULL));
  for (int u = 0; u < TEST_Q_LOOP_CNT; u++) {
    size_t prod_fct = getRandom (4, 32);
    size_t cons_fct = getRandom (4, 32);
    size_t sz = getRandom(TEST_Q_BUFSIZE_MIN, TEST_Q_BUFSIZE_MAX);
    uint8_t * buf = NULL;
    LG_INFO("Starting test run No.: %u with a queue of size 0x%08X (%6.2fMB) and producer/consumer factors at %u/%u.", u + 1, sz, B2MB * sz, prod_fct, cons_fct);

    buf = calloc(1, sz);
    if (buf == NULL) {
      LG_ERROR("Can't allocate memory.");
      continue;
    }

    mq_init(buf, sz);

    for (int i = 0; i < TEST_Q_LOOP_CNT_PER_Q; i++) {
      size_t produced = 0;
      size_t consumed = 0;
      size_t free_sz  = 0;
      produced = getRandom(0, TEST_Q_MSGS_MAX / prod_fct);
      if (mq_produce_msgs(produced))
        return -1;
      consumed = getRandom(1, TEST_Q_MSGS_MAX / cons_fct);
      if (mq_consume_msgs(consumed))
          return -1;
      free_sz = mq_get_free();
      LG_INFO("Message Queue turn #%3u - %6u produced and %6u consumed -> free memory: %5.1f%% (%5.2fMByte)\r", i, produced, consumed,  (100.0f * free_sz) / sz, B2MB * free_sz);
    }
    mq_consume_msgs(TEST_Q_MSGS_MAX);
    if (mq_peek() != NULL) {
      LG_CRITICAL("Could not drain the queue...");
      return -1;
    }
    free(buf);
    if (mq_test.read != mq_test.write){
      LG_CRITICAL("Queue is empty but there are more expected (...");
      return -1;
    }
  }
  return 0;
}


int main(int argc, char *argv[]) {

  parseArgs(argc, argv, &cfg);

  log_init("ems_test",  cfg.log_facility, cfg.log_level);

  log_push(LL_NONE, "############################################################################################");
  log_push(LL_NONE, "Starting %s "APP_VERSION" - on:%s, LogFacility:%s Level:%s.",
           cfg.prg_name, cfg.serial_device, log_get_facility_name(cfg.log_facility), log_get_level_name(cfg.log_level, TRUE));
  log_push(LL_NONE, "############################################################################################");
  // Set signal handler

  return loop();
}

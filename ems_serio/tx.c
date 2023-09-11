#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#include "serial.h"
#include "ctrl/com/ems.h"
#include "ems_serio.h"
#include "rx.h"
#include "crc.h"
#include "msg_queue.h"

int tx_retries = -1;
static uint8_t client_id = CLIENT_ID;
enum STATE state = RELEASED;


static ssize_t tx_packet(uint8_t * msg, size_t len)
{
  size_t i;
  uint8_t echo;
  int ret;

  print_telegram(1, len > 1 ? LL_INFO : LL_DEBUG_MORE, "TX", msg, len);

  // Write the message by character while checking the echoed characters from the MASTER_ID
  for (i = 0; i < len; i++)
  {
    if (serial_push_byte(msg[i]) != 1)
    {
      LG_ERROR("write() failed");
      return -1;
    }
    for (int u = 0; u < 2; u++)
    {
      if ((ret = serial_pop_byte(&echo)) != 1)
        return -1;

      if (msg[i] != echo)
      {
        LG_ERROR("TX fail: send 0x%02x but echo is 0x%02x", msg[i], echo);
        return -1;
      }
    }
  }

  serial_send_break();
  if ((ret = serial_pop_byte(&echo)) == 0)
  {
    LG_ERROR("BREAK Echo not received after 200 ms");
    return -1;
  }
  if (ret != SERIAL_RX_BREAK)
  {
    LG_ERROR("TX fail: packet not ACKed by MASTER_ID.");
    return -1;
  }
  return i;
}

static void tx_release()
{
  uint8_t release = client_id | 0x80U;
  if (tx_packet(&release, 1) != 1)
    LG_ERROR("TX poll reply 'bus release' failed.");
  state = RELEASED;
}

int check_for_bus_timeout(struct timeval got_bus)
{
  struct timeval now;
  int32_t have_bus;
  gettimeofday(&now, NULL);
  have_bus = (now.tv_sec - got_bus.tv_sec) * 1000 + (now.tv_usec - got_bus.tv_usec) / 1000;
  if (have_bus > MAX_BUS_TIME) {
    LG_WARN("Occupying bus since %li ms. Releasing (the bus, not the kraken)...", have_bus);
    tx_release();
    return -1;
  }
  return 0;
}


void handle_poll(struct timeval got_bus)
{
  ssize_t ret;
  state = ASSIGNED;

  if (check_for_bus_timeout(got_bus))
    return;
  // We got polled by the MASTER_ID. Send a message or release the bus.
  // Todo: Send more than one message
  if (tx_retries < 0)
  {
    // Pick a new message
    struct mq_message * msg = mq_peek();
    if (msg)
    {
      tx_retries = 0;
      if (msg->len >= 6)
      {
        // Set the CRC value
        msg->buf[msg->len - 1] = calc_crc(msg->buf, msg->len - 1);
      }
    }
  }

  if (tx_retries >= 0)
  {
    struct mq_message * msg = mq_peek();
    if ((size_t) tx_packet(msg->buf, msg->len) == msg->len)
    {
      tx_retries = -1;
      if (msg->buf[1] == 0x00)             // broadcast
      {
        tx_release();
      } else if ((msg->buf[1] & 0x80))     // read request
      {
        read_expected[0] = msg->buf[1] | 0x80;
        read_expected[1] = msg->buf[0];
        read_expected[2] = msg->buf[2];
        read_expected[3] = msg->buf[3];
        state = READ;                    // Write command
      } else
      {
        state = WROTE;
      }
      mq_pull();
    } else
    {
      LG_WARN("TX failed, %i/%i", tx_retries, MAX_TX_RETRIES);
      tx_retries++;
      if (tx_retries > MAX_TX_RETRIES)
      {
        LG_WARN("TX failed %d times. Dropping message.", MAX_TX_RETRIES);
        tx_retries = -1;
        mq_pull();
        tx_release();
      }
    }
  }else
    tx_release();
}

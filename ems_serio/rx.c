#include <stdint.h>
#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>

#include "serial.h"
#include "ems_serio.h"
#include "tx.h"
#include "crc.h"
#include "ctrl/com/mqtt.h"
#include "tool/logger.h"
#include "ctrl/com/ems.h"

size_t rx_len;
uint8_t rx_buf[MAX_PACKET_SIZE];
enum STATE state = RELEASED;
uint8_t polled_id = 0;
uint8_t read_expected[HDR_LEN];
struct timeval got_bus;
static uint8_t client_id = 0x0BU;

int rx_wait() {
    fd_set rfds;
    struct timeval tv;

    // Wait maximum 200 ms for the BREAK
    FD_ZERO(&rfds);
    FD_SET(port, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 1000*200; // 200 ms
    return(select(FD_SETSIZE, &rfds, NULL, NULL, &tv));
}

int rx_break() {
    // Read a BREAK from the MASTER_ID.
    int ret;
    uint8_t echo;

    ret = rx_wait();
    if (ret != 1) {
        LOG_ERROR("select() failed: %i", ret);
        return(-1);
    }
    for (size_t i = 0; i < sizeof(BREAK_IN) - 1; i++) {
        ret = read(port, &echo, 1);
        if (ret != 1 || echo != BREAK_IN[i]) {
            LOG_ERROR("TX fail: expected break char 0x%02x but got 0x%02x", echo,
                BREAK_IN[i]);
            return(-1);
        }
    }
    return(0);
}

enum parity_state
{
  PAST_NONE,
  PAST_ESCAPED,
  PAST_ZEROED,
  PAST_BREAK,
  PAST_ERROR
};

const uint8_t break_chars[] = { 0xFFu, 0x00U, 0x00U };

// Loop that reads single characters until a full packet is received.
void rx_packet(int * abort) {

  uint8_t c;
  size_t valid_char;
  enum parity_state parity = PAST_NONE;
  unsigned int parity_errors = 0;

  rx_len = 0;

  while (*abort != 1)
  {
    valid_char = FALSE;
    if (read(port, &c, 1) != 1) {
      usleep(1000);
      continue;
    }

    switch (parity)
    {
      case PAST_NONE:
        if (c == 0xFFU)
          parity = PAST_ESCAPED;
        else
          valid_char = TRUE;
        break;
      case PAST_ESCAPED:
        if (c != 0x00U)
        {
          if (c == 0xFFU)
          {
            valid_char = TRUE;
            parity = PAST_NONE;
          }
          else
            parity = PAST_ERROR;
        }
        else
          parity = PAST_ZEROED;
        break;
      case PAST_ZEROED:
        if (c != 0x00U)
          parity = PAST_NONE;
        else
          parity = PAST_BREAK;
        break;
      case PAST_ERROR:
        parity = PAST_NONE;
        break;
    }
    if (valid_char)
    {
      rx_buf[rx_len++] = c;

      if (rx_len >= MAX_PACKET_SIZE)
      {
        LOG_ERROR("Maximum packet size reached. Following characters ignored.");
        print_telegram(0, LL_INFO, "ABANDONED", rx_buf, rx_len);
        rx_len = 0;
      }
    }
    else if (parity == PAST_BREAK)
    {
      parity = PAST_NONE;
      if (rx_len == 1 || calc_crc(rx_buf, rx_len - 1) == rx_buf[rx_len - 1])         // if there is valid data it shall be provided.
        return;
      print_telegram(0, LL_ERROR, "CRC_ERR", rx_buf, rx_len);
      rx_len = 0;
    }
  }
}

void rx_mac()
{
  // MASTER_ID poll requests (bus assigns) have bit 7 cleared (0x80), since it's HT3 we're talking here.
  // Bus release messages is the device ID, between 8 and 127 (0x08-0x7f) with bit 7 set.
  // When a device has the bus, it can:
  // - Broadcast a message (destination is 0x00) (no response)
  // - Send a write request to another device (destination is device ID) (ACKed with 0x01)
  // - Read another device (desination is ORed with 0x80) (Answer comes immediately)
  print_telegram(0, LL_DEBUG_MORE, "MAC", rx_buf, rx_len);
  if (rx_buf[0] == 0x01) {
      // Got an ACK. Warn if there was no write from the bus-owning device.
      if (state != WROTE) {
          LOG_ERROR("Got an ACK without prior write message from 0x%02hhx", polled_id);
          stats.rx_mac_errors++;
      }
      if (polled_id == client_id) {
          // The ACK is for us after a write command. We can send another message.
          handle_poll();
      } else {
          state = ASSIGNED;
      }
  } else if (rx_buf[0] >= 0x08 && rx_buf[0] < 0x80) {
      // Bus release.
      if (state != ASSIGNED) {
 //       LOG_DEBUG("Got bus release from 0x%02hhx without prior poll request", rx_buf[0]);
          stats.rx_mac_errors++;
      }
      polled_id = 0;
      state = RELEASED;
  } else if (rx_buf[0] & 0x80) {
      // Bus assign. We may not be in released state it the queried device did not exist.
      if (state != RELEASED && state != ASSIGNED) {
 //         LOG_DEBUG("Got bus assign to 0x%02hhx without prior bus release from %02hhx", rx_buf[0], polled_id);
          stats.rx_mac_errors++;
      }
      polled_id = rx_buf[0] & 0x7f;
      if (polled_id == client_id) {
          gettimeofday(&got_bus, NULL);
          handle_poll();
      } else {
          state = ASSIGNED;
      }
  } else {
//      LOG_DEBUG("Ignored unknown MAC package 0x%02hhx", rx_buf[0]);
      stats.rx_mac_errors++;
  }
}

// Handler on a received packet
void rx_done() {
    uint8_t dst;
    int crc;
 
    // Handle MAC packages first. They always have length 1.
    if (rx_len == 1)
      return rx_mac();

    stats.rx_total++;
    if (rx_len < 6) {
        LOG_WARN("Ignored short package");
        if (state == WROTE || state == READ)
            state = ASSIGNED;
        stats.rx_short++;
        return;
    }

    crc = calc_crc(rx_buf, rx_len - 1);
    if (crc != rx_buf[rx_len-1])
    {
      LOG_ERROR("Got an CRC error: 0x%02X : 0x%02X.", crc, rx_buf[rx_len-1]);
      print_telegram(0, LL_ERROR, "CRC-ERROR", rx_buf, rx_len);
    }
    else
    {
      struct ems_telegram * tel = (struct ems_telegram *) rx_buf;
      ems_swap_telegram(tel, rx_len);
      ems_log_telegram(tel, rx_len);
      ems_publish_telegram(mqtt, tel, rx_len);
      return;
    }

    // The MASTER_ID can always send when the bus is not assigned (as it's senseless to poll himself).
    // This implementation does not implement the bus timeouts, so it may happen that the MASTER_ID
    // sends while this program still thinks the bus is assigned.
    // So simply accept messages from the MASTER_ID and reset the state if it was not a read request
    // from a device to the MASTER_ID
    if (rx_buf[0] == 0x88 && (state != READ || memcmp(read_expected, rx_buf, HDR_LEN))) {
        state = RELEASED;
    } else if (state == ASSIGNED) {
        if (rx_buf[0] != polled_id && rx_buf[0] != MASTER_ID) {
//            LOG_ERROR("Ignored package from 0x%02hhx instead of polled 0x%02hhx or MASTER_ID", rx_buf[0], polled_id);
            stats.rx_sender++;
            return;
        }
        dst = rx_buf[1] & 0x7f;
        if (rx_buf[1] & 0x80) {
            if (dst < 0x08) {
 //               LOG_ERROR("Ignored read from 0x%02hhx to invalid address 0x%02hhx",  rx_buf[0], dst);
                stats.rx_format++;
                return;
            }
            // Write request, prepare immediate answer
            read_expected[0] = dst;
            read_expected[1] = rx_buf[0];
            read_expected[2] = rx_buf[2];
            read_expected[3] = rx_buf[3];
            state = READ;
        } else {
            if (dst > 0x00 && dst < 0x08) {
 //               LOG_ERROR("Ignored write from 0x%02hhx to invalid address 0x%02hhx", rx_buf[0], dst);
                stats.rx_format++;
                return;
            }
            if (dst >= 0x08) {
                state = WROTE;
            }
            // Else is broadcast, do nothing than forward.
        }
    } else if (state == READ) {
        // Handle immediate read response.
        state = ASSIGNED;
        if (memcmp(read_expected, rx_buf, HDR_LEN)) {
//            LOG_ERROR("Ignored not expected read header: %02hhx %02hhx %02hhx %02hhx",  rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
            stats.rx_format++;
            return;
        }
        if (polled_id == client_id) {
            handle_poll();
        }
    } else if (state == WROTE) {
//        LOG_ERROR("Received package from 0x%02hhx when waiting for write ACK", rx_buf[0]);
        stats.rx_sender++;
        return;
    } else if (rx_buf[0] != MASTER_ID) {
//        LOG_ERROR("Received package from 0x%02hhx when bus is not assigned", rx_buf[0]);
        stats.rx_sender++;
        return;
    }

    // Do not check the CRC here. It adds too much delay and we risk missing a poll cycle.
    stats.rx_success++;
}


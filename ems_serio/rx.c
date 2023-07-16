#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "serial.h"
#include "ems_serio.h"
#include "tx.h"
#include "crc.h"
#include "ctrl/com/mqtt.h"
#include "tool/logger.h"
#include "ctrl/com/ems.h"

size_t rx_len;
uint8_t rx_buf[MAX_PACKET_SIZE];

uint8_t polled_id = 0;
uint8_t read_expected[HDR_LEN];
struct timeval got_bus;
static uint8_t client_id = 0x0BU;


enum parity_state
{
  PAST_NONE,
  PAST_ESCAPED,
  PAST_ZEROED,
  PAST_BREAK,
  PAST_ERROR
};


// Loop that reads single characters until a full packet is received.
void rx_packet(int * abort) {

  uint8_t c;
  unsigned int data_abandoned = FALSE;
  int ret;

  rx_len = 0;

  while (*abort != 1)
  {
    ret = serial_pop_byte(&c);

    if (ret <= 0)
      continue;
    else if (ret == SERIAL_RX_BREAK)
    {
      if (data_abandoned == FALSE)
      {
        if (rx_len == 1 || calc_crc(rx_buf, rx_len - 1) == rx_buf[rx_len - 1])         // if there is valid data it shall be provided.
          return;
        print_telegram(0, LL_ERROR, "CRC_ERR", rx_buf, rx_len);
      }
      data_abandoned = FALSE;
      rx_len = 0;
      continue;
    }
    if (rx_len == MAX_PACKET_SIZE)
    {
      if (data_abandoned == FALSE) {
        LG_ERROR("Maximum packet size reached. Following characters ignored.");
        print_telegram(0, LL_ERROR, "ABANDONED", rx_buf, rx_len);
        data_abandoned = TRUE;
      }
      print_telegram(0, LL_ERROR, "ABANDONED", &c, 1);
    }
    else
      rx_buf[rx_len++] = c;
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
  uint8_t mac =rx_buf[0];

  print_telegram(0, LL_DEBUG_MORE, "MAC", rx_buf, rx_len);
  if (mac == 0x01) {
      // Got an ACK. Warn if there was no write from the bus-owning device.
      if (state != WROTE) {
          LG_ERROR("Got an ACK without prior write message from 0x%02hhx", polled_id);
          stats.rx_mac_errors++;
      }
      if (polled_id == client_id) {
          // The ACK is for us after a write command. We can send another message.
          handle_poll(got_bus);
      } else {
          state = ASSIGNED;
      }
  } else if (mac >= 0x88) {
      // Bus release.
      if (state != ASSIGNED) {
        LG_DEBUG("Got bus release from 0x%02hhx without prior poll request", mac);
          stats.rx_mac_errors++;
      }
      polled_id = 0;
      state = RELEASED;
  } else if (!(mac & 0x80)) {
      // Bus assign. We may not be in released state if the last queried device did not exist.
      if (state != RELEASED && state != ASSIGNED) {
        LG_DEBUG("Got bus assign to 0x%02hhx without prior bus release from %02hhx", mac, polled_id);
        stats.rx_mac_errors++;
      }
      polled_id = mac;
      if (polled_id == client_id) {
          gettimeofday(&got_bus, NULL);
          handle_poll(got_bus);
      } else {
          state = ASSIGNED;
      }
  } else {
      LG_DEBUG("Ignored unknown MAC package 0x%02hhx", mac);
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
      print_telegram(0, LL_WARN, "Ignored short telegram", rx_buf, rx_len);
      if (state == WROTE || state == READ)
        state = ASSIGNED;
      stats.rx_short++;
      return;
    }

    crc = calc_crc(rx_buf, rx_len - 1);
    if (crc != rx_buf[rx_len-1])
    {
      LG_ERROR("Got an CRC error: 0x%02X : 0x%02X.", crc, rx_buf[rx_len-1]);
      print_telegram(0, LL_ERROR, "CRC-ERROR", rx_buf, rx_len);
    }
    else
    {
      struct ems_telegram * tel = (struct ems_telegram *) rx_buf;
      ems_swap_telegram(tel, rx_len);
      ems_log_telegram(tel, rx_len);
      ems_publish_telegram(mqtt, tel, rx_len);
      ems_logic_evaluate_telegram(tel, rx_len);
    }

    // The MASTER_ID can always send when the bus is not assigned (as it's senseless to poll himself).
    // This implementation does not implement the bus timeouts, so it may happen that the MASTER_ID
    // sends while this program still thinks the bus is assigned.
    // So simply accept messages from the MASTER_ID and reset the state if it was not a read request
    // from a device to the MASTER_ID
    if (rx_buf[0] == 0x88 && (state != READ || memcmp(read_expected, rx_buf, HDR_LEN))) {
        state = RELEASED;
    } else if (state == ASSIGNED) {
        if ((rx_buf[0] & 0x7F) != polled_id && (rx_buf[0] & 0x7F) != MASTER_ID) {
            LG_ERROR("Ignored package from 0x%02hhx instead of polled 0x%02hhx or MASTER_ID", rx_buf[0], polled_id);
            stats.rx_sender++;
            return;
        }
        dst = rx_buf[1] & 0x7f;
        if (rx_buf[1] & 0x80) {
            if (dst < 0x08) {
                LG_ERROR("Ignored read from 0x%02hhx to invalid address 0x%02hhx",  rx_buf[0], dst);
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
                LG_ERROR("Ignored write from 0x%02hhx to invalid address 0x%02hhx", rx_buf[0], dst);
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
            LG_ERROR("Ignored not expected read header: %02hhx %02hhx %02hhx %02hhx",  rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
            stats.rx_format++;
            return;
        }
        if (polled_id == client_id) {
            handle_poll();
        }
    } else if (state == WROTE) {
        LG_ERROR("Received package from 0x%02hhx when waiting for write ACK", rx_buf[0]);
        stats.rx_sender++;
        return;
    } else if (rx_buf[0] != MASTER_ID) {
        LG_ERROR("Received package from 0x%02hhx when bus is not assigned", rx_buf[0]);
        stats.rx_sender++;
        return;
    }

    // Do not check the CRC here. It adds too much delay and we risk missing a poll cycle.
    stats.rx_success++;
}


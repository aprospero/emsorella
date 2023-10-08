#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "serial.h"
#include "tools/crc.h"
#include "tools/stats.h"
#include "linuxtools/ctrl/com/mqtt.h"
#include "linuxtools/ctrl/logger.h"
#include "ctrl/com/ems.h"
#include "ctrl/com/state.h"

size_t rx_len;
uint8_t rx_buf[MAX_PACKET_SIZE];

uint8_t polled_id = 0;
static uint8_t client_id = CLIENT_ID;


int rx_mac()
{
  // MASTER_ID poll requests (bus assigns) have bit 7 cleared (0x80), since it's HT3 we're talking here.
  // Bus release messages is the device ID, between 8 and 127 (0x08-0x7f) with bit 7 set.
  // When a device has the bus, it can:
  // - Broadcast a message (destination is 0x00) (no response)
  // - Send a write request to another device (destination is device ID) (ACKed with 0x01)
  // - Read another device (desination is ORed with 0x80) (Answer comes immediately)
  uint8_t mac =rx_buf[0];
  int do_update_tx = FALSE;

  print_telegram(0, LL_DEBUG_MORE, "MAC", rx_buf, rx_len);

  int is_bus_released = (mac & 0x80) ? TRUE : FALSE;
  mac &= ~0x80;

  if (mac == 0x01) {
      // Got an ACK. Warn if there was no write from the bus-owning device.
      if (!state_is(WROTE)) {
          LG_ERROR("Got an ACK without prior write message from 0x%02hhx", polled_id);
          stats.rx_mac_errors++;
      }
      if (polled_id == client_id) {
          // The ACK is for us after a write command. We can send another message.
        do_update_tx = TRUE;
      } else {
        state_set(ASSIGNED);
      }
  } else if (is_bus_released) {
      // Bus release.
      if (!state_is(ASSIGNED)) {
        LG_DEBUG("Got bus release from 0x%02hhx without prior poll request", mac);
          stats.rx_mac_errors++;
      }
      polled_id = 0;
      state_set(RELEASED);
  } else if (mac >= 0x08) { // lower mac addresses seem to be forbidden?
      // Bus assign. We may not be in released state if the last queried device did not exist.
      if (!(state_is(RELEASED) || state_is(ASSIGNED))) {
        LG_DEBUG("Got bus assign to 0x%02hhx without prior bus release from %02hhx. Actual state: %s.", mac, polled_id, state_get_str());
        stats.rx_mac_errors++;
      }
      polled_id = mac;
      state_set(ASSIGNED);
      if (polled_id == client_id) {  // we have aquired the bus and can begin to send msgs.
          state_get_bus();
          do_update_tx = TRUE;
      }
  } else {
      LG_DEBUG("Ignored unknown MAC package 0x%02hhx", mac);
      stats.rx_mac_errors++;
  }
  return do_update_tx;
}

// Handler on a received packet - returns TRUE/FALSE if tx_update should be called.
static int rx_done() {
  uint8_t dst;
  int do_update_tx = FALSE;
  int crc;

  // Handle MAC packages first. They always have length 1.
  if (rx_len == 1)
    return rx_mac();

  stats.rx_total++;
  if (rx_len < 6) {
    print_telegram(0, LL_WARN, "Ignored short telegram", rx_buf, rx_len);
    if (state_is(WROTE) || state_is(READ))
      state_set(ASSIGNED);
    stats.rx_short++;
    goto end_of_done;
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
    ems_publish_telegram(tel, rx_len);
    ems_logic_evaluate_telegram(tel, rx_len);
  }

  // The MASTER_ID can always send when the bus is not assigned (as it's senseless to poll himself).
  // This implementation does not implement the bus timeouts, so it may happen that the MASTER_ID
  // sends while this program still thinks the bus is assigned.
  // So simply accept messages from the MASTER_ID and reset the state if it was not a read request
  // from a device to the MASTER_ID
  if (rx_buf[0] == 0x08 && (!state_is(READ) || state_cmp_expected(rx_buf))) {
    state_set(RELEASED);
    stats.rx_success++;
    goto end_of_done;
  }

  switch (state_get())
  {
    case ASSIGNED:
      if ((rx_buf[0] & 0x7F) != polled_id && (rx_buf[0] & 0x7F) != MASTER_ID) {
          LG_ERROR("Ignored packet from 0x%02x instead of polled 0x%02x or MASTER_ID", rx_buf[0], polled_id);
          stats.rx_sender++;
          goto end_of_done;
      }
      dst = rx_buf[1] & 0x7f;
      if (rx_buf[1] & 0x80) {
          if (dst < 0x08) {
              LG_ERROR("Ignored read from 0x%02hhx to invalid address 0x%02hhx",  rx_buf[0], dst);
              stats.rx_format++;
              goto end_of_done;
          }
          // Write request, prepare immediate answer
          state_set_expected(rx_buf);
          state_set(READ);
      } else {
          if (dst > 0x00 && dst < 0x08) {
              LG_ERROR("Ignored write from 0x%02hhx to invalid address 0x%02hhx", rx_buf[0], dst);
              stats.rx_format++;
              goto end_of_done;
          }
          if (dst >= 0x08) {
              state_set(WROTE);
          }
          // Else is broadcast, do nothing than forward.
      }
      break;
    case READ:
      // Handle immediate read response.
      state_set(ASSIGNED);
      if (state_cmp_expected(rx_buf)) {
          LG_ERROR("Ignored not expected read header: %02hhx %02hhx %02hhx %02hhx",  rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
          stats.rx_format++;
          goto end_of_done;
      }
      if (polled_id == client_id) // received answer on read request, we can send next msg.
        do_update_tx = TRUE;
      break;
    case WROTE:
      LG_ERROR("Received package from 0x%02hhx when waiting for write ACK", rx_buf[0]);
      stats.rx_sender++;
      goto end_of_done;
    case RELEASED:
      if (rx_buf[0] != MASTER_ID) {
        LG_ERROR("Received package from 0x%02hhx when bus is not assigned", rx_buf[0]);
        stats.rx_sender++;
        goto end_of_done;
      }
      break;
    default:
      LG_ERROR("We're in an invalid state: %d - how could that happen?!", state_get());
      goto end_of_done;
      break;
  }
  // Do not check the CRC here. It adds too much delay and we risk missing a poll cycle.
  stats.rx_success++;

end_of_done:

  return do_update_tx;
}



// Loop that reads single characters until a full packet is received.
int rx_packet(volatile int * abort) {

  uint8_t c;
  unsigned int data_abandoned = FALSE;
  int ret;

  rx_len = 0;

  while (!*abort)
  {
    ret = serial_pop_byte(&c);

    if (ret <= 0)
      continue;
    else if (ret == SERIAL_RX_BREAK)
    {
      if (data_abandoned == FALSE)
      {
        if (rx_len == 1 || calc_crc(rx_buf, rx_len - 1) == rx_buf[rx_len - 1])         // if there is valid data it shall be provided.
          break;
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

  return *abort ? FALSE : rx_done();
}




#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>

#include "serial.h"
#include "ctrl/com/ems.h"
#include "ems_serio.h"
#include "rx.h"
#include "crc.h"

int tx_retries = -1;
uint8_t tx_buf[MAX_PACKET_SIZE];
size_t tx_len = 0;
static uint8_t client_id = CLIENT_ID;

ssize_t tx_packet(uint8_t *msg, size_t len) {
    size_t i;
    uint8_t echo;

    print_telegram(1, LL_INFO, "TX", msg, len);

    // Write the message by character while checking the echoed characters from the MASTER_ID
    for (i = 0; i < len; i++) {
        LG_DEBUG("WR 0x%02hhx", msg[i]);
        if (write(port, &msg[i], 1) != 1) {
            LG_ERROR("write() failed");
            return(i);
        }
        if (rx_wait() != 1) {
            LG_ERROR("Echo not received after 200 ms");
            return(i);
        }
        if (read(port, &echo, 1) != 1) {
            LG_ERROR("read() failed after successful select");
            return(i);
        };
        LG_DEBUG("RD 0x%02hhx", echo);
        if (msg[i] != echo) {
            LG_ERROR("TX fail: send 0x%02x but echo is 0x%02x", msg[i], echo);
            return(i);
        }
        if (echo == 0xff) {
            // Parity escaping also doubles a 0xff
            if (read(port, &echo, 1) != 1) {
                LG_ERROR("read() failed");
                return(i);
            }
            LG_DEBUG("RD 0x%02hhx", echo);
            if (echo != 0xff) {
                LG_ERROR("TX fail: parity escaping expected 0xff but got 0x%02x", echo);
                return(i);
            }
        }
    }

    serial_send_break();
    if (rx_break() == -1) {
        LG_ERROR("TX fail: packet not ACKed by MASTER_ID");
        return(0);
    }

    return(i);
}

void handle_poll() {
    ssize_t ret;
    struct timeval now;
    int32_t have_bus;

    // We got polled by the MASTER_ID. Send a message or release the bus.
    // Todo: Send more than one message
    // Todo: Release the bus after sending a message (does not work)
    if (tx_retries < 0 || tx_retries > MAX_TX_RETRIES) {
        if (tx_retries > MAX_TX_RETRIES) {
            LG_ERROR("TX failed 5 times. Dropping message.");
            tx_retries = -1;
        }
        // Pick a new message
        // TODO: implement a send message trigger mechanism with buffering
        if (tx_len > 0) {
            tx_retries = 0;
            if (tx_len >= 6) {
                // Set the CRC value
                tx_buf[tx_len - 1] = calc_crc(tx_buf, tx_len - 1);
            }
        }
    }

    gettimeofday(&now, NULL);
    have_bus = (now.tv_sec - got_bus.tv_sec) * 1000 + (now.tv_usec - got_bus.tv_usec) / 1000;
    LG_INFO("Occupying bus since %li ms", have_bus);

    if (tx_retries >= 0 && have_bus < MAX_BUS_TIME) {
        if ((size_t) tx_packet(tx_buf, tx_len) == tx_len) {
            tx_retries = -1;
            tx_len = 0;
            if (tx_buf[1] == 0x00) {  // broadcast
                // Release bus
                if (tx_packet(&client_id, 1) != 1)
                  LG_ERROR("TX poll reply failed");
                state = RELEASED;
            } else if (tx_buf[1] & 0x80) {
                read_expected[0] = tx_buf[1] & 0x7f;
                read_expected[1] = tx_buf[0];
                read_expected[2] = tx_buf[2];
                read_expected[3] = tx_buf[3];
                state = READ;
            } else {
                // Write command
                state = WROTE;
            }
        } else {
            LG_ERROR("TX failed, %i/%i", tx_retries, MAX_TX_RETRIES);
            tx_retries++;
            state = RELEASED;
        }
    } else {
      // Nothing to send.
      uint8_t release = client_id | 0x80U;
      if (tx_packet(&release, 1) != 1)
        LG_ERROR("TX poll reply failed");
      state = RELEASED;
    }
}

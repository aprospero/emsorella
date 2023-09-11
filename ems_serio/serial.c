#define _GNU_SOURCE 1

#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "tool/logger.h"
#include "serial.h"

// LEGACY
#define SERIAL_TX_BIT_TIME 104                             // bit time @9600 baud

// HT3/Junkers - 7 bit delay.
#define SERIAL_TX_WAIT (SERIAL_TX_BIT_TIME * 7)
#define SERIAL_TX_BRK  (SERIAL_TX_BIT_TIME * 11)


int port;
struct termios tios;
uint32_t * volatile uart_reg = NULL;
int mem_fd = -1;

#define UART_REG_BASE 0x02020000
#define UART_REG_LEN  0x00001000

#define UART_FLG_UCR1_SNDBRK (0x01 << 4)
#define UART_FLG_USR2_TXDC   (0x01 << 3)

#define UART_REG_UCR1 (0x80 >> 2)
#define UART_REG_USR2 (0x98 >> 2)



int serial_open(const char *tty_path) {
    // Opens a raw serial with parity marking enabled
    int ret, fd;

    mem_fd = open("/dev/mem", O_RDWR);
    if (-1 == mem_fd) {
      LG_ERROR("Could not open /dev/mem.");
      return -1;
    }
    uart_reg = mmap(NULL, UART_REG_LEN, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, UART_REG_BASE);
    if (uart_reg == MAP_FAILED) {
       LG_ERROR("Could not mmap");
       return 1;
    }

    port = open(tty_path, O_RDWR | O_NOCTTY);
    if (port < 0) {
        return(port);
    }

    ret = tcgetattr(port, &tios);
    if (ret != 0) {
        return(ret);
    }

    // Raw mode: Noncanonical mode, no input processing, no echo, no signals, no modem control.
    tios.c_cflag &= ~HUPCL;
    tios.c_cflag |= (CLOCAL | CREAD);

    tios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ISIG | IEXTEN |
                      ECHOPRT | ECHOCTL | ECHOKE);
    tios.c_oflag &= ~(OPOST | ONLCR | OCRNL);
    tios.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK | BRKINT | IUCLC );

    // Enable parity marking.
    // This is important as each telegramme is terminated by a BREAK signal.
    // Without it, we could not distinguish between two telegrammes.
    tios.c_iflag &= ~(IGNPAR );
    tios.c_iflag |= (PARMRK | INPCK);

    // 8 bits per character
    tios.c_cflag &= ~CSIZE;
    tios.c_cflag |= CS8;
    // No parity bit
    tios.c_iflag &= ~(ISTRIP);
    tios.c_cflag &= ~(PARENB | PARODD | 010000000000);
    // One stop bit
    tios.c_cflag &= ~CSTOPB;
    // No hardware or software flow control
    tios.c_iflag &= ~(IXON | IXOFF);
    tios.c_cflag &= ~CRTSCTS;
    // Buffer
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;

    // 9600 baud
    ret = cfsetispeed(&tios, B9600);
    ret |= cfsetospeed(&tios, B9600);
    if (ret != 0) {
        return(ret);
    }

    ret = tcsetattr(port, TCSANOW, &tios);
    if (ret != 0) {
      LG_ERROR("could not set Serial Params: %s.", strerror(errno));
      return ret;
    }
    tcflush(port, TCIOFLUSH);
    return(0);
}

int serial_close() {
  int ret = 0;
  int tmp;
  if (uart_reg != NULL) {
    ret = munmap(uart_reg, UART_REG_LEN);
    if (ret != 0) {
       LG_ERROR("Could not munmap");
     }
  }
  if (mem_fd != -1) {
    tmp = close (mem_fd);
    if (tmp) {
      LG_ERROR("Could not close /dev/mem");
      ret |= tmp;
    }
  }
  return(ret | close(port));
}

void serial_send_break()
{
  // wait for pending transmissions done
  while (!(uart_reg[UART_REG_USR2] & UART_FLG_USR2_TXDC))
    if (log_get_level_state(LL_DEBUG))
      LG_DEBUG("Wait for TX to complete...");
  usleep(SERIAL_TX_WAIT);
  // send break for at least 1,004 ms (10 zeros at 9600 baud)
  uart_reg[UART_REG_UCR1] |= UART_FLG_UCR1_SNDBRK;
  usleep(SERIAL_TX_BRK);
  uart_reg[UART_REG_UCR1] &= ~UART_FLG_UCR1_SNDBRK;
}

//static inline int pop_byte(uint8_t * buf)
//{
//  int ret= read(port, buf, 1);
//  if (ret < 0)
//    LG_ERROR("Serial Port read error: %s", strerror(errno));
//  else if (ret == 0)
//    LG_ERROR("Serial Port read none.");
//  else if (ret == 1)
//    LG_DEBUG("RD 0x%02x", *buf);
//  return ret;
//}
//
//int serial_pop_byte(uint8_t * buf)
//{
//  int ret= pop_byte(buf);
//  if (ret <= 0)
//    return ret;
//  else if (*buf == 0xFF)  /* escape char */
//  {
//    do {
//      ret = pop_byte(buf);
//    } while (ret == 0);
//    if (ret < 0)
//      return ret;
//    else if (*buf == 0xFF)  /* escaped 0xFF */
//      return ret;
//    else if (*buf == 0x00) /* BREAK received? */
//    {
//      do {
//        ret = pop_byte(buf);
//      } while (ret == 0);
//      if (ret < 0)
//        return ret;
//      else if (*buf == 0x00) /* BREAK received! */
//        return SERIAL_RX_BREAK;
//    }
//  }
//  else
//    return ret;
//
//  LG_ERROR("Serial received invalid escape character: 0x%02x", *buf);
//  return -1;
//
//}

int serial_wait() {
    fd_set rfds;
    struct timeval tv;

    // Wait maximum 200 ms for the BREAK
    FD_ZERO(&rfds);
    FD_SET(port, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 1000 * 200; // 200 ms
    return (select(FD_SETSIZE, &rfds, NULL, NULL, &tv));
}

static uint8_t last_sent = 0x00;

int serial_pop_byte(uint8_t * buf)
{
  int ret, cnt = 0;

  while (TRUE) {
    if (serial_wait() == 0 || (ret = read(port, buf, 1)) == 0)
    {
      LG_ERROR("Serial Port read timeout (200ms).");
      return 0;
    }
    if (ret < 0) {
      LG_ERROR("Serial Port read error: %s", strerror(errno));
      return ret;
    }
    else
    {
      LG_DBGMX("RD 0x%02x", *buf);
      switch (++cnt)
      {
        case 1: if      (*buf != 0xFF) return ret; /* default case     */
                else                   continue;   /* escape character */
                break;
        case 2: if      (*buf == 0xFF) return ret; /* escaped 0xFF     */
                else if (*buf == 0x00) continue;   /* escaped BREAK ?  */
                break;
        case 3: if      (*buf == 0x00 || *buf == last_sent) return SERIAL_RX_BREAK; /* escaped BREAK ! */
                break;
      }
      LG_ERROR("Serial received invalid escape character: 0x%02x", *buf);
      return -1;
    }
  }
}


int serial_push_byte(uint8_t byte)
{
  while (!(uart_reg[UART_REG_USR2] & UART_FLG_USR2_TXDC))
    if (log_get_level_state(LL_DEBUG))
      LG_DEBUG("Wait for TX to complete...");
  usleep(SERIAL_TX_WAIT);
  int ret = write(port, &byte, 1);
  if (ret < 0)
    LG_ERROR("Serial Port write error: %s", strerror(errno));
  else if (ret == 1) {
    last_sent = byte;
    LG_DBGMX("WR 0x%02x", byte);
    // wait for pending transmissions done

  }
  return ret;
}

#define _GNU_SOURCE 1

#include <termios.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "tool/logger.h"

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
    tios.c_iflag &= ~(INLCR | IGNCR | ICRNL | IGNBRK | BRKINT | IUCLC);

    // Enable parity marking.
    // This is important as each telegramme is terminated by a BREAK signal.
    // Without it, we could not distinguish between two telegrammes.
    tios.c_iflag |= PARMRK;

    // 9600 baud
    ret = cfsetispeed(&tios, B9600);
    ret |= cfsetospeed(&tios, B9600);
    if (ret != 0) {
        return(ret);
    }
    // 8 bits per character
    tios.c_cflag &= ~CSIZE;
    tios.c_cflag |= CS8;
    // No parity bit
    tios.c_iflag &= ~(INPCK | ISTRIP);
    tios.c_cflag &= ~(PARENB | PARODD | 010000000000);
    // One stop bit
    tios.c_cflag &= ~CSTOPB;
    // No hardware or software flow control
    tios.c_iflag &= ~(IXON | IXOFF);
    tios.c_cflag &= ~CRTSCTS;
    // Buffer
    tios.c_cc[VMIN] = 1;
    tios.c_cc[VTIME] = 0;
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

  // send break for at least 1,004 ms (10 zeros at 9600 baud)
  uart_reg[UART_REG_UCR1] |= UART_FLG_UCR1_SNDBRK;
  usleep(1004);
  uart_reg[UART_REG_UCR1] &= ~UART_FLG_UCR1_SNDBRK;
}

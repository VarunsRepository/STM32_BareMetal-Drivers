#ifndef UART_H
#define UART_H

/* USART2 on the Nucleo-F401RE, wired to the ST-Link's virtual COM
   port - so output arrives on your PC over the same USB cable that
   flashes the board. Nothing extra to connect.

   Open it with PuTTY, screen, or VS Code's serial monitor at the
   baud rate you pass to uart_init(). On Windows it appears as a
   COM port; find it with:

       Get-WmiObject Win32_SerialPort | Select Name, DeviceID

   Deliberately no printf. Pulling in newlib's formatter costs
   several KB of flash and wants a heap; for streaming ECG samples a
   plain integer-to-string is all that is needed, and it is twenty
   lines. */

#include <stdint.h>


/* Configures PA2/PA3 and USART2. Call after clock_init_84mhz(),
   because the baud rate is computed from the bus clock. */
void uart_init(uint32_t baud);

/* Sends one byte. Blocks until the transmit register is free -
   which at 115200 baud is about 87 us per character. Fine for
   debug output; too slow to call from inside a 200 Hz sampling
   interrupt, where it would eat the whole budget. */
void uart_putc(char c);

/* Sends a zero-terminated string. */
void uart_print(const char *s);

/* Sends a signed integer in decimal, followed by nothing - add
   your own separator or newline. Handles negatives, which matters
   once ECG samples are centred around zero. */
void uart_print_int(int32_t value);

/* The two conveniences used most: a value with a label, and a
   newline. */
void uart_print_line(const char *label, int32_t value);

#endif /* UART_H */

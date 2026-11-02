#include "uart.h"
#include "clock.h"
#include "mcu.h"
#include "gpio.h"


/* ---- Where USART2 lives ---- */

#define USART_2  0x40004400UL   /* on APB1 */

#define UART_STATUS   0x00   /* flags: is the transmitter free yet?  */
#define UART_DATA     0x04   /* write a byte here to send it         */
#define UART_BAUD     0x08   /* the clock divisor - see below        */
#define UART_CONTROL1 0x0C   /* enable the peripheral, TX and RX     */

/* Bits in UART_STATUS */
#define TX_EMPTY     (1U << 7)   /* the holding register is free      */
#define TX_COMPLETE  (1U << 6)   /* the last bit has left the pin     */
#define RX_READY     (1U << 5)   /* a byte has arrived                */

/* Bits in UART_CONTROL1 */
#define RX_ENABLE    (1U << 2)
#define TX_ENABLE    (1U << 3)
#define UART_ENABLE  (1U << 13)

/* The pins. PA2 and PA3 are routed on the board to the ST-Link, so
   these specific two are what reach the PC. Any other USART2 pin
   mapping would need a wire. */
#define UART_PORT  PORT_A
#define PIN_TX     2
#define PIN_RX     3

#define CLOCK_USART_2  17   /* bit 17 of RCC_APB1ENR */


void uart_init(uint32_t baud)
{
    /* 1. Clocks. Two of them: the port, and the peripheral. */
    clock_on_ahb1(CLOCK_PORT_A);
    clock_on_apb1(CLOCK_USART_2);

    /* 2. Hand both pins to USART2.

       PIN_AS_ALTERNATE alone is not enough - it says "a peripheral
       drives this", not which one. AF_USART2 (7) picks it. Get the
       number wrong and the pin goes to a different peripheral, so
       nothing comes out and nothing complains. */
    gpio_alternate(UART_PORT, PIN_TX, AF_USART2);
    gpio_alternate(UART_PORT, PIN_RX, AF_USART2);

    /* 3. The baud rate divisor.

       The register holds a fixed-point number: 12 bits of whole
       part, 4 bits of sixteenths. Which sounds fiddly until you
       notice what it works out to -

           USARTDIV = bus_clock / (16 * baud)
           register = USARTDIV * 16
                    = bus_clock / baud

       - so the whole fixed-point business cancels out and the value
       to write is simply the clock divided by the baud rate. The
       + baud/2 rounds to nearest rather than truncating.

       USART2 is on APB1, so APB1_HZ (42 MHz) is the right figure
       here - NOT the doubled 84 MHz that APB1's TIMERS get. The
       doubling applies to timers only. Using 84 here would halve
       every character's duration and the PC would see garbage.

       At 42 MHz and 115200 this gives 365, an actual rate of
       115068 - 0.11% off, far inside the ~2% a UART tolerates. */
    REG(USART_2, UART_BAUD) = (APB1_HZ + baud / 2) / baud;

    /* 4. Switch on the transmitter, the receiver, and the
       peripheral itself. Defaults handle the rest: 8 data bits,
       no parity, 1 stop bit - the usual 8N1. */
    REG(USART_2, UART_CONTROL1) = TX_ENABLE | RX_ENABLE | UART_ENABLE;
}


void uart_putc(char c)
{
    /* Wait for the holding register to empty. The hardware is still
       shifting the previous character out one bit at a time, and
       writing early would overwrite it mid-flight.

       At 115200 baud a character takes about 87 us, so this loop
       can block for that long. Harmless in main(); ruinous inside a
       200 Hz interrupt, which only has 5 ms total. */
    while (!(REG(USART_2, UART_STATUS) & TX_EMPTY)) {
    }

    REG(USART_2, UART_DATA) = (uint32_t)c;
}


void uart_print(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}


void uart_print_int(int32_t value)
{
    /* 11 characters is enough for any 32-bit value: ten digits plus
       a minus sign. */
    char buffer[12];
    int  i = 0;
    uint32_t magnitude;

    if (value < 0) {
        uart_putc('-');
        /* Cast before negating. INT32_MIN has no positive
           counterpart, so -value would overflow - which is
           undefined behaviour, and the compiler is entitled to
           assume it never happens. Going through unsigned is
           well defined. */
        magnitude = (uint32_t)(-(int64_t)value);
    } else {
        magnitude = (uint32_t)value;
    }

    /* Digits come out backwards - least significant first - so
       collect them then send in reverse. */
    do {
        buffer[i++] = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0);

    while (i > 0) {
        uart_putc(buffer[--i]);
    }
}


void uart_print_line(const char *label, int32_t value)
{
    uart_print(label);
    uart_print_int(value);
    uart_print("\r\n");   /* \r as well as \n - some terminals need it */
}

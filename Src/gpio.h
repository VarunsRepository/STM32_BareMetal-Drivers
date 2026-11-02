#ifndef GPIO_H
#define GPIO_H

/* Pin control for the STM32F401RE.
   All of these are 'static inline': at -O1 and above the compiler
   replaces the call with the one instruction inside it, so the
   readable version costs exactly the same as the bit twiddle. */

#include "mcu.h"


/* ---- The registers inside a GPIO port ---- */

#define GPIO_MODE      0x00   /* what each pin IS: input/output/alternate/analog */
#define GPIO_PULL      0x0C   /* internal pull-up or pull-down resistor */
#define GPIO_INPUT     0x10   /* read the live level of every pin */
#define GPIO_OUTPUT    0x14   /* the level each output pin is driving */
#define GPIO_SET_RESET 0x18   /* atomic set/clear - see gpio_high() below */


/* ---- What a pin can be ----
   Two bits per pin, so pin 5 lives at bits 11 and 10. */

#define PIN_AS_INPUT      0U
#define PIN_AS_OUTPUT     1U
#define PIN_AS_ALTERNATE  2U   /* handed over to a peripheral: UART, SPI, timer */
#define PIN_AS_ANALOG     3U   /* handed over to the ADC */

#define PULL_NONE  0U
#define PULL_UP    1U
#define PULL_DOWN  2U


/* Sets what a pin is. Clears both mode bits first, then sets the
   new value - skipping the clear would OR the new bits onto the old
   ones and could silently leave the pin in a mode you never asked
   for (01 on top of 10 gives 11, which is analog, and the pin goes
   quiet with no error anywhere). */
static inline void gpio_mode(uint32_t port, uint32_t pin, uint32_t mode)
{
    REG(port, GPIO_MODE) &= ~(3U << (pin * 2));
    REG(port, GPIO_MODE) |=  (mode << (pin * 2));
}

static inline void gpio_pull(uint32_t port, uint32_t pin, uint32_t pull)
{
    REG(port, GPIO_PULL) &= ~(3U << (pin * 2));
    REG(port, GPIO_PULL) |=  (pull << (pin * 2));
}


/* ---- Driving a pin ----

   These two use GPIO_SET_RESET rather than the obvious |= and &=~
   on GPIO_OUTPUT, and the difference matters.

   'OUTPUT |= bit' is really three steps: read the register, change
   one bit, write it back. If an interrupt fires in the middle and
   touches the same port, its change is wiped out when we write back.
   Rare, non-deterministic, and horrible to track down.

   SET_RESET is write-only and atomic. The low 16 bits set a pin,
   the high 16 bits clear it, and writing a zero anywhere does
   nothing - so one instruction, nothing to interleave with, and
   other pins are never disturbed. */

static inline void gpio_high(uint32_t port, uint32_t pin)
{
    REG(port, GPIO_SET_RESET) = (1U << pin);
}

static inline void gpio_low(uint32_t port, uint32_t pin)
{
    REG(port, GPIO_SET_RESET) = (1U << (pin + 16));
}

static inline void gpio_write(uint32_t port, uint32_t pin, int high)
{
    if (high) gpio_high(port, pin);
    else      gpio_low(port, pin);
}

/* Toggle has to read first - there is nothing to XOR against
   otherwise - so this one is not atomic. Fine when a single place
   owns the pin, which is the usual case. */
static inline void gpio_toggle(uint32_t port, uint32_t pin)
{
    REG(port, GPIO_OUTPUT) ^= (1U << pin);
}

/* Reads the actual voltage on the pin, which is not always what we
   last wrote - an input, or an output someone else is pulling. */
static inline int gpio_read(uint32_t port, uint32_t pin)
{
    return (REG(port, GPIO_INPUT) >> pin) & 1U;
}

#endif /* GPIO_H */

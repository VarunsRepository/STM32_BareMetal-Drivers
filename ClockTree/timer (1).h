#ifndef TIMER_H
#define TIMER_H

/* General-purpose timer control (TIM2-TIM5) for the STM32F401RE. */

#include "mcu.h"
#include "clock.h"


/* ---- The registers inside a timer ---- */

#define TIM_CONTROL   0x00   /* bit 0 starts the counter */
#define TIM_IRQ_ON    0x0C   /* bit 0 allows an interrupt when it wraps */
#define TIM_FLAG      0x10   /* bit 0 goes high on wrap - we clear it by hand */
#define TIM_COUNT     0x24   /* the live count */
#define TIM_PRESCALE  0x28   /* divides the incoming clock */
#define TIM_WRAP_AT   0x2C   /* the value it counts up to, then wraps */


/* How fast the counter is fed, before the prescaler.

   This now comes from clock.h rather than being typed in here, so
   there is ONE place in the project that knows the clock rate. Before
   clock_init_84mhz() ran, the right answer was 16000000; after it,
   84000000. A stale copy of that number is the classic way to end up
   sampling at the wrong rate with nothing to warn you.

   Note it is APB1_TIMER_HZ, not APB1_HZ. APB1 itself runs at 42 MHz,
   but because its prescaler is not 1 the hardware doubles its timers
   back up to 84 MHz. Using the bus figure gives a rate exactly 2x
   wrong - the single most common STM32 timer bug, and a silent one. */

#define TIMER_INPUT_HZ  APB1_TIMER_HZ


/* Sets a timer running and raising an interrupt 'hz' times a second.

   Two divisions rather than one: the prescaler slows the incoming
   clock to tick_hz, then the counter wraps after however many of
   those slower ticks make up one period. Splitting it this way lets
   a 16-bit counter reach long periods - at 84 MHz a single counter
   could only span about 780 us before overflowing.

   Both registers count zero as a step, hence the -1 on each.

   tick_hz is the intermediate rate. Keep it comfortably above hz and
   well below TIMER_INPUT_HZ; 100 kHz suits anything from a few Hz
   upward, and leaves the wrap value large enough that rounding does
   not bite. For 200 Hz ECG sampling: 100000 / 200 = 500 exactly. */
static inline void timer_start_hz(uint32_t timer, uint32_t tick_hz, uint32_t hz)
{
    REG(timer, TIM_PRESCALE) = (TIMER_INPUT_HZ / tick_hz) - 1;
    REG(timer, TIM_WRAP_AT)  = (tick_hz / hz) - 1;
    REG(timer, TIM_IRQ_ON)  |= (1U << 0);
    REG(timer, TIM_CONTROL) |= (1U << 0);
}

/* Call this FIRST in the handler. The timer does not clear its own
   flag - forget it and the interrupt re-fires the instant we return,
   forever. main() never runs again and the chip looks frozen. */
static inline void timer_clear_flag(uint32_t timer)
{
    REG(timer, TIM_FLAG) &= ~(1U << 0);
}

static inline void timer_stop(uint32_t timer)
{
    REG(timer, TIM_CONTROL) &= ~(1U << 0);
}

#endif /* TIMER_H */

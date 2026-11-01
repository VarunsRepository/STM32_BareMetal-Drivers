#ifndef TIMER_H
#define TIMER_H

/* General-purpose timer control (TIM2-TIM5) for the STM32F401RE. */

#include "mcu.h"


/* ---- The registers inside a timer ---- */

#define TIM_CONTROL   0x00   /* bit 0 starts the counter */
#define TIM_IRQ_ON    0x0C   /* bit 0 allows an interrupt when it wraps */
#define TIM_FLAG      0x10   /* bit 0 goes high on wrap - we clear it by hand */
#define TIM_COUNT     0x24   /* the live count */
#define TIM_PRESCALE  0x28   /* divides the incoming clock */
#define TIM_WRAP_AT   0x2C   /* the value it counts up to, then wraps */


/* How fast the counter is fed, before the prescaler.

   At reset the APB1 prescaler is 1, so TIM2 sees the full 16 MHz
   core clock with no doubling. Once the PLL is configured this
   changes twice over: the core goes to 84 MHz, APB1 must be
   divided by 2 to respect its 42 MHz ceiling, and then the timers
   on APB1 get their clock multiplied BACK up to 84 MHz. Compute a
   rate against 42 and it comes out exactly 2x wrong, silently. */
#define TIMER_INPUT_HZ  16000000UL


/* Sets a timer running and raising an interrupt at 'hz' times a
   second. Two divisions rather than one: the prescaler slows the
   incoming clock, then the counter wraps after so many of those
   slower ticks. Splitting it this way lets a 16-bit counter reach
   long periods. Both registers count zero as a step, hence the -1.

   tick_hz is the intermediate rate. Keep it comfortably above hz
   and below TIMER_INPUT_HZ; 10 kHz suits anything from 1 Hz up. */
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

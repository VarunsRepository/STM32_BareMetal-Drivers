#ifndef CLOCK_H
#define CLOCK_H

/* Clock tree setup for the STM32F401RE on a Nucleo board.
   Call clock_init_84mhz() as the FIRST line of main(), before any
   peripheral is configured - the timer, ADC and UART dividers are
   all computed from the numbers below, so they are wrong if the
   clock changes underneath them afterwards. */

#include <stdint.h>


/* ---- What the tree produces, once configured ----

        8 MHz from the ST-Link
          / 8   =   1 MHz   feeds the phase comparator
          * 336 = 336 MHz   the VCO, forced here by the feedback
          / 4   =  84 MHz   SYSCLK

   and then the three buses:

        AHB   / 1  ->  84 MHz   core, GPIO, DMA, SysTick
        APB1  / 2  ->  42 MHz   USART2, TIM2-5   (42 MHz is its ceiling)
        APB2  / 1  ->  84 MHz   ADC1, TIM1, USART1

   These constants are what the rest of the project computes against,
   so a change here propagates everywhere rather than being retyped. */

#define SYSCLK_HZ   84000000UL
#define AHB_HZ      84000000UL
#define APB1_HZ     42000000UL
#define APB2_HZ     84000000UL

/* Timers on APB1 are the awkward case. Because APB1's prescaler is
   not 1, the hardware multiplies their clock back up to the full
   84 MHz - ST did this so timer resolution is not lost on the slow
   bus. So a timer on APB1 is fed 84 MHz, NOT the 42 MHz the bus
   itself runs at. Compute a sample rate against 42 and it comes out
   exactly 2x wrong, with nothing to warn you. */
#define APB1_TIMER_HZ  84000000UL
#define APB2_TIMER_HZ  84000000UL


/* Sets the chip to 84 MHz from the external 8 MHz reference.
   Blocks for roughly a millisecond while the oscillator settles and
   the PLL locks. Safe to call once, at startup. */
void clock_init_84mhz(void);

#endif /* CLOCK_H */

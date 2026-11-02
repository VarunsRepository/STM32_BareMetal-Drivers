#include "clock.h"
#include "mcu.h"


/* ---- The registers involved ---- */

/* Flash's own control register. Not part of RCC - flash is a
   separate block, and this is the one register outside RCC that the
   clock setup has to touch. */
#define FlashInterface  0x40023C00UL
#define FLASH_ACCESS    REG(FlashInterface, 0x00)

/* Switches oscillators on, and reports when each is stable. */
#define RCC_CONTROL     REG(ResetAndClockControl, 0x00)

/* The three PLL dividers, plus which source feeds it. */
#define RCC_PLL_CONFIG  REG(ResetAndClockControl, 0x04)

/* The bus prescalers, and which source drives the core. */
#define RCC_CONFIG      REG(ResetAndClockControl, 0x08)


/* ---- Bits inside those registers ----
   Note the pattern: nearly everything comes in pairs. One bit to
   ASK for something, another the hardware sets to CONFIRM it has
   happened. We write the first and wait on the second, every time,
   because each of these takes real physical time. */

#define HSE_ON       (1U << 16)   /* start the external input        */
#define HSE_READY    (1U << 17)   /* hardware: it is stable now      */
#define HSE_BYPASS   (1U << 18)   /* it is a finished signal, not a
                                     crystal to be driven - see below */
#define PLL_ON       (1U << 24)   /* start the loop                  */
#define PLL_READY    (1U << 25)   /* hardware: the loop is locked    */


/* ---- The dividers ----

   The field values are NOT the divisors. This is the single easiest
   thing to get wrong here, so each one is spelled out.             */

#define PLL_M        8U    /* 8 MHz / 8 = 1 MHz into the comparator  */
#define PLL_N        336U  /* feedback divider: forces VCO to 336 MHz */
#define PLL_P_CODE   1U    /* 0 means /2, 1 means /4, 2 /6, 3 /8.
                              So 1 gives us 336 / 4 = 84 MHz.        */
#define PLL_Q        7U    /* 336 / 7 = 48 MHz exactly, which is what
                              USB needs. Unused here, but it costs
                              nothing to set correctly now.          */

#define PLL_SRC_HSE  (1U << 22)   /* clear would mean use the HSI    */

#define AHB_DIV_1    (0U << 4)    /* anything below 8 means /1       */
#define APB1_DIV_2   (4U << 10)   /* 4 is the CODE for /2, not the
                                     number 2. 5 is /4, 6 is /8.     */
#define APB2_DIV_1   (0U << 13)

#define SYSCLK_SRC_PLL   2U         /* bits 1:0 - what we request    */
#define SYSCLK_NOW_MASK  (3U << 2)  /* bits 3:2 - what is actually live */
#define SYSCLK_NOW_PLL   (2U << 2)

/* How many cycles flash makes the CPU wait before answering. */
#define FLASH_WAIT_MASK  7U
#define FLASH_WAIT_2     2U


void clock_init_84mhz(void)
{
    /* ---- 1. Make flash patient, while we are still slow ----

       Flash takes about 30 ns to produce a word, no matter how fast
       it is asked. At 16 MHz a cycle is 62 ns, so it comfortably
       keeps up with no waiting. At 84 MHz a cycle is only 12 ns, so
       the CPU has to idle 2 extra cycles (3 x 12 = 36 ns) to give
       flash time to answer.

       Doing this FIRST is free - the CPU simply stalls for cycles it
       did not need, for the few microseconds until we speed up.

       Doing it AFTER the speed-up is fatal, and silently so: the
       very next instruction fetch reads flash before the data is
       valid, the CPU executes whatever noise was on the bus, and
       since the fault handler's address also lives in flash, the
       fault mechanism fails too. The core locks up with no output
       at all. It looks exactly like a dead board.                  */

    FLASH_ACCESS |= FLASH_WAIT_2;
    while ((FLASH_ACCESS & FLASH_WAIT_MASK) != FLASH_WAIT_2) {
        /* The reference manual explicitly says to read this back
           and confirm before raising the clock. */
    }


    /* ---- 2. Switch on the 8 MHz coming from the ST-Link ----

       BYPASS is the important flag. A Nucleo has no crystal: the
       debugger chip on the end of the board feeds in a finished
       square wave. Crystal mode would have the chip try to drive an
       oscillator that is not there, and the wait below would never
       finish - a hang with no error, which is a classic way to lose
       an evening.

       We move off the internal 16 MHz RC oscillator because it is
       only accurate to about 1% and drifts with temperature. The
       external reference is around 20 ppm, 500x better. That matters
       here: whatever error is in this clock lands directly in the
       sampling rate, and from there in every filter cutoff and every
       heart rate the detector reports.                             */

    RCC_CONTROL |= HSE_ON | HSE_BYPASS;
    while (!(RCC_CONTROL & HSE_READY)) {
        /* Roughly a millisecond for the input to settle. */
    }


    /* ---- 3. Set the three dividers, then start the loop ----

       Nothing in a PLL multiplies anything. There is an oscillator
       (the VCO) that can be tuned over a wide range but has no idea
       where it is, and a loop that continuously compares a divided
       copy of its output against the reference and nudges it.

       Putting the divider in the FEEDBACK path is the trick: by
       dividing its own output by 336 before comparing, the VCO is
       forced to run 336 times faster than the reference to keep
       matching. Division, arranged to produce multiplication.

       The two range limits below are real hardware constraints, not
       guidance. Outside them the loop does not lock and the wait
       never finishes:
           PLL input (after /M) must be 1-2 MHz      -> 1 MHz   ok
           VCO       (after *N) must be 100-432 MHz  -> 336 MHz ok      */

    RCC_PLL_CONFIG = (PLL_M      <<  0)
                   | (PLL_N      <<  6)
                   | (PLL_P_CODE << 16)
                   | (PLL_Q      << 24)
                   | PLL_SRC_HSE;

    RCC_CONTROL |= PLL_ON;
    while (!(RCC_CONTROL & PLL_READY)) {
        /* The hardware reporting that the loop is in step. Tens of
           microseconds, during which its output is garbage - which
           is exactly why we do not switch to it before checking. */
    }


    /* ---- 4. Divide the buses down, BEFORE anything speeds up ----

       SYSCLK is still 16 MHz here, so these dividers barely do
       anything yet. That is the whole point of setting them now:
       when the switch is flipped in step 5, APB1 is ALREADY halved
       and never sees 84 MHz.

       The other order would briefly run APB1 at 84 against its
       42 MHz ceiling. That usually appears to work, then corrupts
       UART bytes under load - the sort of fault that gets blamed on
       wiring for a week.                                           */

    RCC_CONFIG |= AHB_DIV_1 | APB1_DIV_2 | APB2_DIV_1;


    /* ---- 5. Flip the core over ----

       One write, and the chip changes speed. Everything above ran at
       16 MHz; everything after this runs at 84.

       SW (bits 1:0) is what we ask for. SWS (bits 3:2) is what the
       hardware says is actually live. They are separate because the
       switch takes a few cycles, so we wait for the confirmation
       rather than assuming.                                        */

    RCC_CONFIG |= SYSCLK_SRC_PLL;
    while ((RCC_CONFIG & SYSCLK_NOW_MASK) != SYSCLK_NOW_PLL) {
        /* Now running at 84 MHz. */
    }
}

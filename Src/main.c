#include "clock.h"
#include "mcu.h"
#include "gpio.h"
#include "timer.h"

/* The green user LED, LD2, is on pin 5 of port A. */
#define LED_PORT  PORT_A
#define LED_PIN   5


/* The hardware jumps here when TIM2 wraps. The name must be exactly
   this - the startup file already reserved its slot in the vector
   table, and a misspelling just defines an unused function with no
   error anywhere. */
void TIM2_IRQHandler(void)
{
    /* Clear the flag FIRST. The timer does not clear its own, and if
       we return without doing it the interrupt re-fires immediately,
       forever - main() never runs again and the chip looks frozen. */
    timer_clear_flag(TIMER_2);

    gpio_toggle(LED_PORT, LED_PIN);
}


int main(void)
{
    /* FIRST, before anything else. Every divider set below is
       computed from the clock rate, so all of them are wrong if the
       clock changes underneath them afterwards. */
    clock_init_84mhz();

    clock_on_ahb1(CLOCK_PORT_A);
    clock_on_apb1(CLOCK_TIMER_2);

    gpio_mode(LED_PORT, LED_PIN, PIN_AS_OUTPUT);

    irq_enable(IRQ_TIMER_2);

    /* 10 kHz internal tick, wrapping once a second. The LED toggles
       on each wrap, so it is on for a second and off for a second -
       a two-second full cycle.

       This is the test for the clock tree. Count 20 toggles against a
       stopwatch: it should take 20 seconds. If clock_init_84mhz() had
       silently failed and we were still on the 16 MHz HSI, each
       toggle would take 5.25 seconds instead of 1, and 20 of them
       would take nearly two minutes. Not a subtle difference. */
    timer_start_hz(TIMER_2, 10000, 1);

    while (1)
    {
        /* The LED is entirely the interrupt's job, so main() is free.
           This is the structure the ECG sampling will use: the
           interrupt does the periodic work, main() stays idle. */
    }
}
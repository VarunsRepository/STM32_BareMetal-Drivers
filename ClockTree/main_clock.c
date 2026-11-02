#include "clock.h"
#include "mcu.h"
#include "gpio.h"
#include "timer.h"

/* The green user LED, LD2, is on pin 5 of port A. */
#define LED_PORT  PORT_A
#define LED_PIN   5


/* The hardware jumps here when TIM2 wraps. */
void TIM2_IRQHandler(void)
{
    timer_clear_flag(TIMER_2);
    gpio_toggle(LED_PORT, LED_PIN);
}


int main(void)
{
    /* FIRST, before anything else. Every divider set below is
       computed from the clock rate, so they are all wrong if the
       clock changes afterwards. */
    clock_init_84mhz();

    clock_on_ahb1(CLOCK_PORT_A);
    clock_on_apb1(CLOCK_TIMER_2);

    gpio_mode(LED_PORT, LED_PIN, PIN_AS_OUTPUT);

    irq_enable(IRQ_TIMER_2);

    /* 100 kHz internal tick, wrapping once a second. The LED toggles
       on each wrap, so it is on for a second and off for a second -
       a 0.5 Hz blink.

       This is the test: time it against a watch over 30 seconds. If
       the clock tree is right it will be exact. If something in the
       PLL setup is wrong but the chip still runs, the rate will be
       visibly off - 16/84 is a factor of 5.25, which you cannot
       miss. Getting this wrong silently is exactly what the clock
       setup is guarding against. */
    timer_start_hz(TIMER_2, 100000, 1);

    while (1)
    {
        /* The LED is entirely the interrupt's job. */
    }
}

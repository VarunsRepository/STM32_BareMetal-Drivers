#include "mcu.h"
#include "gpio.h"
#include "timer.h"

/* The green user LED, LD2, is on pin 5 of port A.
   The blue user button, B1, is on pin 13 of port C. */
#define LED_PORT  PORT_A
#define LED_PIN   5

#define BUTTON_PORT PORT_C
#define BUTTON_PIN  13


/* The hardware jumps here when TIM2 wraps. The name must be exactly
   this - the startup file already reserved its slot in the vector
   table, and a misspelling just defines an unused function with no
   error anywhere. */
void TIM2_IRQHandler(void)
{
    timer_clear_flag(TIMER_2);
    gpio_toggle(LED_PORT, LED_PIN);
}


int main(void)
{
    /* 1. clock, before anything else touches these peripherals */
    clock_on_ahb1(CLOCK_PORT_A);
    clock_on_ahb1(CLOCK_PORT_C);
    clock_on_apb1(CLOCK_TIMER_2);

    /* 2. pins */
    gpio_mode(LED_PORT, LED_PIN, PIN_AS_OUTPUT);
    gpio_mode(BUTTON_PORT, BUTTON_PIN, PIN_AS_INPUT);

    /* 3. interrupt */
    irq_enable(IRQ_TIMER_2);

    /* 4. go: 10 kHz internal tick, wrapping twice a second */
    timer_start_hz(TIMER_2, 10000, 2);

    while (1)
    {
        /* The LED is entirely the interrupt's job, so main() is
           free. The button on this board is active-low: pressed
           reads 0, released reads 1. */
    }
}

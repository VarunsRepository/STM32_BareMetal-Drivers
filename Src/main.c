#include "clock.h"
#include "mcu.h"
#include "gpio.h"
#include "timer.h"
#include "uart.h"

/* The green user LED, LD2, is on pin 5 of port A. */
#define LED_PORT  PORT_A
#define LED_PIN   5


/* Incremented by the interrupt, read by main(). 'volatile' because
   main() would otherwise read it once and cache it in a register,
   never noticing the interrupt's updates. */
static volatile uint32_t ticks = 0;


/* The hardware jumps here when TIM2 wraps. Note what is NOT in
   here: no printing. uart_putc() blocks for ~87 us per character,
   and this is the structure the 200 Hz sampler will use, where the
   whole budget is 5 ms. The interrupt sets a flag; main() does the
   slow work. */
void TIM2_IRQHandler(void)
{
    timer_clear_flag(TIMER_2);
    ticks++;
    gpio_toggle(LED_PORT, LED_PIN);
}


int main(void)
{
    /* FIRST. Every divider below is computed from the clock rate -
       the UART's baud divisor included - so all of them are wrong
       if the clock changes underneath them afterwards. */
    clock_init_84mhz();

    clock_on_ahb1(CLOCK_PORT_A);
    clock_on_apb1(CLOCK_TIMER_2);

    gpio_mode(LED_PORT, LED_PIN, PIN_AS_OUTPUT);

    uart_init(115200);
    uart_print("\r\nF401RE up at 84 MHz\r\n");

    irq_enable(IRQ_TIMER_2);
    timer_start_hz(TIMER_2, 10000, 1);   /* wrap once a second */

    uint32_t last_reported = 0;

    while (1)
    {
        /* Print only when the count has moved. Polling a flag set
           by the interrupt, rather than printing from inside it. */
        if (ticks != last_reported) {
            last_reported = ticks;
            uart_print_line("tick ", (int32_t)last_reported);
        }
    }
}
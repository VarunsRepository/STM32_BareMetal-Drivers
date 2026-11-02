#include <stdint.h>

/* ---- Where the hardware lives in memory ---- */

/* RCC = Reset and Clock Control. Switches power/clocks on and off
   for every peripheral. Nothing works until its clock is on. */
#define ResetAndClockControl   0x40023800UL

/* The port that pin PA5 (the green LED, LD2) belongs to. */
#define PORTA_GreenLED 0x40020000UL

/* SysTick = a countdown timer built into the CPU core itself.
   Same address on every ARM Cortex-M chip, and it needs no clock
   switching on, because it is part of the core rather than a peripheral. */
#define SysTickTimer   0xE000E010UL


/* ---- The registers we need ---- */

/* One bit per port. Bit 0 switches on port A. */
#define CLOCK_ENABLE (*(volatile uint32_t *)(ResetAndClockControl + 0x30))

/* Decides what each pin IS - input, output, analog, etc.
   Two bits per pin, so pin 5 occupies bits 11 and 10. */
#define PIN_MODE     (*(volatile uint32_t *)(PORTA_GreenLED + 0x00))  //this is similar to arduino's PinMode

/* One bit per pin. Writing 1 drives the pin high. */
#define PIN_OUTPUT   (*(volatile uint32_t *)(PORTA_GreenLED + 0x14))

/* Switches the timer on, picks its clock, and enables its interrupt. */
#define TIMER_CONTROL (*(volatile uint32_t *)(SysTickTimer + 0x00))

/* The value the timer counts down FROM. Reaching zero fires the
   interrupt, then it reloads from here and counts down again. */
#define TIMER_RELOAD  (*(volatile uint32_t *)(SysTickTimer + 0x04))

/* The live count, right now. Writing any value clears it. */
#define TIMER_NOW     (*(volatile uint32_t *)(SysTickTimer + 0x08))


#define LED 5   /* the LED is on pin 5 of port A */

/* The chip is running on its internal 16 MHz oscillator, so 16,000
   ticks take 1 ms. Zero counts as a tick, so the reload is one less. */
#define CPU_SPEED      16000000UL // because the CPU genuinely doesn't "know" its clock speed ;)
#define TICKS_PER_MS   (CPU_SPEED / 1000 - 1)


/* Counts milliseconds since boot. It must be 'volatile' because the
   interrupt changes it behind main()'s back - without that, the compiler
   would read it once, keep the value in a register, and never notice. */
static volatile uint32_t milliseconds = 0;


/* We never call this. The hardware jumps here on its own every time
   the timer reaches zero. The name has to be exactly this, because the
   startup file already reserved a slot for it in the vector table. */
void SysTick_Handler(void)
{
    milliseconds++;
}


/* Waits by watching the counter, instead of burning nops.
   Subtracting handles the 32-bit rollover correctly. */
static void wait_ms(uint32_t ms)
{
    uint32_t started_at = milliseconds;
    while ((milliseconds - started_at) < ms) {
        /* nothing to do - the interrupt is doing the counting */
    }
}


int main(void)
{
    CLOCK_ENABLE |= (1U << 0);            /* power up port A */

    PIN_MODE &= ~(3U << (LED * 2));       /* clear pin 5's two mode bits */
    PIN_MODE |=  (1U << (LED * 2));       /* set them to 01 = output */

    TIMER_RELOAD  = TICKS_PER_MS;         /* one millisecond per countdown */
    TIMER_NOW     = 0;                    /* clear whatever was in there */
    TIMER_CONTROL = (1U << 0)             /* bit 0: start counting        */
                  | (1U << 1)             /* bit 1: interrupt on zero     */
                  | (1U << 2);            /* bit 2: run off the CPU clock */

    while (1)
    {
        PIN_OUTPUT ^= (1U << LED);        /* flip the LED on/off */
        wait_ms(500);
    }
}

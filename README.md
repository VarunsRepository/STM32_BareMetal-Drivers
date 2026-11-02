

main.c       clock_init_84mhz()  →  clock on peripherals  →  configure pins
                                 →  enable interrupt  →  start timer  →  idle
clock.c      the register sequence that gets to 84 MHz
clock.h      the resulting rates, which everything else computes from
mcu.h        base addresses, REG(), clock-enable and NVIC helpers
gpio.h       pin mode, read, write, toggle
timer.h      start a timer at a given frequency, clear its flag


mcu.h — base addresses from RM0368, and REG(base, offset) which builds the volatile pointer. The GPIO ports sit 1 KB apart, so the same offsets work on all of them. Also the clock_on_* helpers, because an unclocked peripheral reads back as zero and fails silently.

gpio.h — gpio_high/gpio_low use BSRR (offset 0x18), which is write-only and atomic: low 16 bits set a pin, high 16 bits clear it. Avoids the read-modify-write race that ODR |= has. gpio_toggle can’t avoid it, so it’s the one non-atomic operation.

clock.c — five ordered steps: flash wait states, HSE on, PLL configured and locked, bus prescalers, switch SYSCLK. Each step waits on a confirmation bit because the hardware takes real time.

clock.h — exports SYSCLK_HZ, APB1_HZ, APB2_HZ, APB1_TIMER_HZ. This is the only place the clock rate is written down.

timer.h — timer_start_hz computes prescaler and wrap from APB1_TIMER_HZ, so it stays correct across clock changes.

The clock tree
8 MHz (ST-Link, bypass mode)
  ÷ M=8    →   1 MHz    PLL input, must be 1–2 MHz
  × N=336  → 336 MHz    VCO, must be 100–432 MHz
  ÷ P=4    →  84 MHz    SYSCLK

AHB  ÷1 → 84 MHz   core, GPIO, DMA
APB1 ÷2 → 42 MHz   TIM2–5, USART2   (42 MHz ceiling)
APB2 ÷1 → 84 MHz   ADC1, TIM1

#ifndef MCU_H
#define MCU_H

/* Base addresses and clock switches for the STM32F401RE.
   Everything here comes from the reference manual, RM0368. */

#include <stdint.h>


/* ---- Where each block of hardware lives ---- */

#define ResetAndClockControl  0x40023800UL   /* clock switches, PLL, bus prescalers */
#define InterruptController   0xE000E100UL   /* NVIC: lets peripheral IRQs reach the CPU */

/* The GPIO ports sit exactly 1 KB apart, so the same offsets
   work on every one of them - only the base address changes. */
#define PORT_A  0x40020000UL
#define PORT_B  0x40020400UL
#define PORT_C  0x40020800UL
#define PORT_D  0x40020C00UL
#define PORT_E  0x40021000UL
#define PORT_H  0x40021C00UL

#define TIMER_2 0x40000000UL                 /* on APB1 */


/* ---- Reaching a register ---- */

/* Every register is one 32-bit word at a fixed offset from its
   block's base. REG() builds the volatile pointer and dereferences
   it, so REG(PORT_A, 0x14) IS port A's output register - readable
   and writable like an ordinary variable.

   'volatile' tells the compiler the value can change on its own,
   so it must read and write the real address every single time
   rather than caching it in a CPU register. */
#define REG(base, offset)  (*(volatile uint32_t *)((base) + (offset)))


/* ---- Switching peripheral clocks on ----

   Every peripheral is clock-gated: with its clock off it is inert,
   its registers read back as zero and writes vanish silently. This
   is the single most common bare-metal bug, so it gets its own
   named functions rather than a bare bit twiddle. */

#define RCC_AHB1ENR  0x30   /* GPIO ports, DMA, CRC */
#define RCC_APB1ENR  0x40   /* TIM2-5, USART2, I2C  */
#define RCC_APB2ENR  0x44   /* ADC1, TIM1, USART1   */

/* Bit numbers within those registers */
#define CLOCK_PORT_A  0
#define CLOCK_PORT_B  1
#define CLOCK_PORT_C  2
#define CLOCK_TIMER_2 0
#define CLOCK_ADC_1   8

static inline void clock_on_ahb1(uint32_t bit) { REG(ResetAndClockControl, RCC_AHB1ENR) |= (1U << bit); }
static inline void clock_on_apb1(uint32_t bit) { REG(ResetAndClockControl, RCC_APB1ENR) |= (1U << bit); }
static inline void clock_on_apb2(uint32_t bit) { REG(ResetAndClockControl, RCC_APB2ENR) |= (1U << bit); }


/* ---- Letting an interrupt through to the CPU ----

   A peripheral can raise its flag all day; until its bit is set
   here the CPU never hears about it. One bit per interrupt source,
   numbered in the vector table order. */

#define IRQ_TIMER_2  28

static inline void irq_enable(uint32_t irq_number)
{
    REG(InterruptController, 0x00) = (1U << irq_number);
}

#endif /* MCU_H */

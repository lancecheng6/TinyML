// timebase.h — millisecond timebase on the DWT cycle counter (480 MHz core clock)
// NOTE: the Wildfire HAL_GetTick override has 1-second resolution, which is too
// coarse for key debouncing and short delays. CYCCNT is verified running.
#ifndef H743_TIMEBASE_H
#define H743_TIMEBASE_H

#include "stm32h7xx.h"

static inline void tb_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55u;          // unlock (Cortex-M7)
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t tb_cycles(void) { return DWT->CYCCNT; }

static inline uint32_t tb_ms(void) {
  return DWT->CYCCNT / (SystemCoreClock / 1000u);
}

static inline void tb_delay_ms(uint32_t ms) {
  uint32_t start = DWT->CYCCNT;
  uint32_t n = ms * (SystemCoreClock / 1000u);
  while ((DWT->CYCCNT - start) < n) { }
}

#endif

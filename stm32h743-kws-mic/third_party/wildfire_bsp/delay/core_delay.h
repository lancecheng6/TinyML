#ifndef __CORE_DELAY_H
#define __CORE_DELAY_H

#include "stm32h7xx.h"

/* Get the core clock frequency */
#define GET_CPU_ClkFreq()       HAL_RCC_GetSysClockFreq()
#define SysClockFreq            (480000000)
/* For convenience, the delay functions call CPU_TS_TmrInit internally to
   initialize the timestamp registers, so they are re-initialized on every call.
   Set this macro to 0 and call CPU_TS_TmrInit once at the start of main() to
   avoid re-initializing every time. */

#define CPU_TS_INIT_IN_DELAY_FUNCTION   0


/*******************************************************************************
 * Function declarations
 ******************************************************************************/
uint32_t CPU_TS_TmrRd(void);
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority);
uint32_t HAL_GetTick(void);
// Call CPU_TS_TmrInit (or enable CPU_TS_INIT_IN_DELAY_FUNCTION) before using
// the functions below. The maximum delay is 8 seconds.
void CPU_TS_Tmr_Delay_US(uint32_t us);
#define HAL_Delay(ms)     CPU_TS_Tmr_Delay_US(ms*1000)
#define CPU_TS_Tmr_Delay_S(s)       CPU_TS_Tmr_Delay_MS(s*1000)


#endif /* __CORE_DELAY_H */

/**
  ******************************************************************
  * @file    core_delay.c
  * @author  fire
  * @version V1.0
  * @date    2018-xx-xx
  * @brief   Precise delay using core registers
  ******************************************************************
  * @attention
  *
  * Platform : Wildfire STM32H743 development board
  * Forum    : http://www.firebbs.cn
  * Shop     : https://fire-stm32.taobao.com
  *
  ******************************************************************
  */
  
#include "./delay/core_delay.h"   


/*
**********************************************************************
*         Timestamp-related register definitions
**********************************************************************
*/
/*
  Cortex-M has a peripheral called DWT (Data Watchpoint and Trace).
  It contains a 32-bit register called CYCCNT, an up-counter that counts
  core clock cycles. The longest measurable interval is:
  10.74 s = 2^32 / 400000000
  (assuming a 400 MHz core; one core cycle is about 1/400M = 2.5 ns)
  When CYCCNT overflows it wraps back to 0 and keeps counting up.
  Steps to enable CYCCNT counting:
  1) Enable the DWT peripheral: bit 24 (TRCENA) of DEMCR.
  2) Clear CYCCNT to 0 before enabling it.
  3) Enable CYCCNT: bit 0 of DWT_CTRL (DWT_CR) set to 1.
 */


#define  DWT_CR      *(__IO uint32_t *)0xE0001000
#define  DWT_CYCCNT  *(__IO uint32_t *)0xE0001004
#define  DEM_CR      *(__IO uint32_t *)0xE000EDFC


#define  DEM_CR_TRCENA                   (1 << 24)
#define  DWT_CR_CYCCNTENA                (1 <<  0)


/**
  * @brief  Initialize the timestamp counter
  * @param  None
  * @retval None
  * @note   Must be called before using the delay functions
  */
HAL_StatusTypeDef HAL_InitTick(uint32_t TickPriority)
{
    /* Enable the DWT peripheral */
    DEM_CR |= (uint32_t)DEM_CR_TRCENA;                

    /* Clear the DWT CYCCNT counter */
    DWT_CYCCNT = (uint32_t)0u;

    /* Enable the Cortex-M DWT CYCCNT counter */
    DWT_CR |= (uint32_t)DWT_CR_CYCCNTENA;
  
    return HAL_OK;
}

/**
  * @brief  Read the current timestamp
  * @param  None
  * @retval Current timestamp (the value of DWT_CYCCNT)
  */
uint32_t CPU_TS_TmrRd(void)
{        
  return ((uint32_t)DWT_CYCCNT);
}

/**
  * @brief  Read the current timestamp
  * @param  None
  * @retval Current timestamp (the value of DWT_CYCCNT)
  */
uint32_t HAL_GetTick(void)
{        
  return ((uint32_t)DWT_CYCCNT/SysClockFreq*1000);
}


/**
  * @brief  Precise delay based on the CPU cycle counter (32-bit)
  * @param  us : delay in microseconds
  * @retval None
  * @note   Call CPU_TS_TmrInit (or enable CPU_TS_INIT_IN_DELAY_FUNCTION)
  *         before using this function. Maximum delay is 8 s (8*1000*1000).
  */
void CPU_TS_Tmr_Delay_US(uint32_t us)
{
  uint32_t ticks;
  uint32_t told,tnow,tcnt=0;

  /* Optionally initialize the timestamp registers inside the function */
#if (CPU_TS_INIT_IN_DELAY_FUNCTION)  
  /* Initialize the timestamp and clear it */
  HAL_InitTick(5);
#endif
  
  ticks = us * (GET_CPU_ClkFreq() / 1000000);  /* required number of ticks */
  tcnt = 0;
  told = (uint32_t)CPU_TS_TmrRd();         /* counter value on entry */

  while(1)
  {
    tnow = (uint32_t)CPU_TS_TmrRd();  
    if(tnow != told)
    { 
      /* The 32-bit counter counts up */
      if(tnow > told)
      {
        tcnt += tnow - told;  
      }
      /* counter wrapped around */
      else 
      {
        tcnt += UINT32_MAX - told + tnow; 
      } 
      
      told = tnow;

      /* Exit when the elapsed time reaches the requested delay */
      if(tcnt >= ticks)break;
    }  
  }
}

/*********************************************END OF FILE**********************/

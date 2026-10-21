/* h743_uart.c — bare-metal USART1 driver for STM32H743 (PA9=TX / PA10=RX) @115200
 * Purpose: route TFLM DebugLog output to the onboard CH340 -> host COMx for automated checks.
 * Clock: post-reset default HSI 64 MHz; USART1 is on APB2 -> PCLK2 = 64 MHz.
 */

#include "stm32h743xx.h"
#include <stdint.h>

#ifndef USART_ISR_TXE_TXFNF
#define USART_ISR_TXE_TXFNF USART_ISR_TXE
#endif

void uart_init(void) {
  /* 1) Enable clocks: GPIOA + USART1 */
  RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  (void)RCC->APB2ENR;  /* dummy read (required by H7 RCC) */

  /* 2) PA9 / PA10 -> Alternate Function 7 (USART1) */
  GPIOA->MODER &= ~((3u << (9 * 2)) | (3u << (10 * 2)));
  GPIOA->MODER |= (2u << (9 * 2)) | (2u << (10 * 2));
  GPIOA->OSPEEDR |= (3u << (9 * 2)) | (3u << (10 * 2));   /* very high speed */
  GPIOA->OTYPER &= ~((1u << 9) | (1u << 10));             /* push-pull */
  GPIOA->PUPDR &= ~((3u << (9 * 2)) | (3u << (10 * 2)));
  GPIOA->PUPDR |= (1u << (10 * 2));                       /* PA10 RX pull-up */
  GPIOA->AFR[1] &= ~((0xFu << ((9 - 8) * 4)) | (0xFu << ((10 - 8) * 4)));
  GPIOA->AFR[1] |= (7u << ((9 - 8) * 4)) | (7u << ((10 - 8) * 4));

  /* 3) USART1: 8N1, 115200 (PCLK2 64MHz -> BRR = 64e6/115200 ~= 556) */
  USART1->CR1 = 0;
  USART1->BRR = (uint32_t)((64000000u + 115200u / 2u) / 115200u);  /* = 556 */
  USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void uart_putc(char c) {
  while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0u) {
  }
  USART1->TDR = (uint8_t)c;
}

void uart_write(const char* s, int n) {
  for (int i = 0; i < n; ++i) {
    uart_putc(s[i]);
  }
}

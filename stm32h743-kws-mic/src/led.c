// led.c — D6 RGB LED (Wildfire H7Pro): R=PB0, G=PB1, B=PA3, active LOW
#include "stm32h7xx_hal.h"

#include "led.h"

#define LED_R_PORT GPIOB
#define LED_R_PIN  GPIO_PIN_0
#define LED_G_PORT GPIOB
#define LED_G_PIN  GPIO_PIN_1
#define LED_B_PORT GPIOA
#define LED_B_PIN  GPIO_PIN_3

void Led_Init(void) {
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Mode = GPIO_MODE_OUTPUT_PP;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin = LED_R_PIN; HAL_GPIO_Init(LED_R_PORT, &g);
  g.Pin = LED_G_PIN; HAL_GPIO_Init(LED_G_PORT, &g);
  g.Pin = LED_B_PIN; HAL_GPIO_Init(LED_B_PORT, &g);
  HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_G_PORT, LED_G_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, GPIO_PIN_SET);
}

// active LOW: RESET = on
void Led_Red(int on)   { HAL_GPIO_WritePin(LED_R_PORT, LED_R_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET); }
void Led_Green(int on) { HAL_GPIO_WritePin(LED_G_PORT, LED_G_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET); }
void Led_Blue(int on)  { HAL_GPIO_WritePin(LED_B_PORT, LED_B_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET); }

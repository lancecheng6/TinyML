// key.c — KEY1 = PA0 (Wildfire H7Pro), active HIGH (external pull-down)
#include "stm32h7xx_hal.h"

#include "key.h"
#include "timebase.h"

#define KEY1_PORT GPIOA
#define KEY1_PIN  GPIO_PIN_0

#define KEY_DEBOUNCE_MS 50

static int s_state = 0;      // 0=idle, 1=debouncing press, 2=waiting release
static uint32_t s_t0 = 0;

void Key_Init(void) {
  GPIO_InitTypeDef g = {0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  g.Pin = KEY1_PIN;
  g.Mode = GPIO_MODE_INPUT;
  g.Pull = GPIO_PULLDOWN;    // board has an external pull-down too
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(KEY1_PORT, &g);
  s_state = 0;
}

int Key1_Poll(void) {
  int hi = (HAL_GPIO_ReadPin(KEY1_PORT, KEY1_PIN) == GPIO_PIN_SET) ? 1 : 0;
  switch (s_state) {
    case 0:
      if (hi) { s_t0 = tb_ms(); s_state = 1; }
      break;
    case 1:
      if (!hi) {
        s_state = 0;                          // bounced: not a valid press
      } else if ((tb_ms() - s_t0) >= KEY_DEBOUNCE_MS) {
        s_state = 2;                          // valid press, now wait for release
      }
      break;
    case 2:
      if (!hi) { s_state = 0; return 1; }     // released -> trigger
      break;
    default:
      s_state = 0;
      break;
  }
  return 0;
}

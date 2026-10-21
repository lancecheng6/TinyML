// person_uart_app.cc — H743 VWW (person / not-person) over UART (RGB565)
//
// PC sends an RGB565 frame (little-endian uint16) over USART1:
//   [0xA5][0x5A][w_lo][w_hi][h_lo][h_hi][ w*h*2 bytes ]
// The board resizes to 96x96 (nearest), converts to grayscale, quantizes to
// int8 and runs the TFLM person_detect model, then prints:
//   RESULT <person|not_person> <p_person> <p_not>
//
// Single-byte commands (outside the frame protocol):
//   'P' -> PONG
//   'D' -> dump the most recent 96x96 int8 tensor (for PC/board parity checks)

#include "stm32h7xx_hal.h"

#include <math.h>
#include <string.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "led.h"
#include "person_detect_model_data.h"
#include "timebase.h"

extern "C" void uart_write(const char* s, int n);

#define IMG_W 96
#define IMG_H 96
#define MAX_SRC_W 320
#define MAX_SRC_H 240

// DMA-free buffers, but the arena is 136 KB > DTCM (128 KB) -> put it in AXI SRAM.
static uint16_t g_frame[MAX_SRC_W * MAX_SRC_H] __attribute__((section(".sram")));
static uint8_t g_arena[136 * 1024] __attribute__((section(".sram")));
static int8_t g_in[IMG_W * IMG_H];
static int8_t g_out[2];

static tflite::MicroInterpreter* g_interp = nullptr;
static int g_have_tensor = 0;

extern "C" void SysTick_Handler(void) { HAL_IncTick(); }

void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.HSIState = RCC_HSI_OFF;
  osc.CSIState = RCC_CSI_OFF;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = 5;
  osc.PLL.PLLN = 192;
  osc.PLL.PLLP = 2;
  osc.PLL.PLLR = 2;
  osc.PLL.PLLQ = 4;
  osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) { while (1) {} }
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 |
                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
  clk.AHBCLKDivider = RCC_HCLK_DIV2;
  clk.APB3CLKDivider = RCC_APB3_DIV2;
  clk.APB1CLKDivider = RCC_APB1_DIV2;
  clk.APB2CLKDivider = RCC_APB2_DIV2;
  clk.APB4CLKDivider = RCC_APB4_DIV2;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) { while (1) {} }
}

static bool InitModel() {
  const tflite::Model* m = tflite::GetModel(g_person_detect_model_data);
  if (m->version() != TFLITE_SCHEMA_VERSION) return false;
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
  resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
  resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
  resolver.AddReshape();
  resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8());
  static tflite::MicroInterpreter interp(m, resolver, g_arena, sizeof(g_arena));
  g_interp = &interp;
  return g_interp->AllocateTensors() == kTfLiteOk;
}

// ---- UART helpers -----------------------------------------------------------
static void clear_errors(void) {
  if (USART1->ISR & USART_ISR_ORE) USART1->ICR = USART_ICR_ORECF;
}

static int uart_read_byte(uint8_t* out, uint32_t timeout_ms) {
  uint32_t t0 = tb_ms();
  for (;;) {
    clear_errors();
    if (USART1->ISR & USART_ISR_RXNE_RXFNE) {
      *out = (uint8_t)USART1->RDR;
      return 1;
    }
    if ((tb_ms() - t0) > timeout_ms) return 0;
  }
}

static int uart_read_buf(uint8_t* buf, int n, uint32_t timeout_ms) {
  uint32_t t0 = tb_ms();
  int got = 0;
  while (got < n) {
    clear_errors();
    if (USART1->ISR & USART_ISR_RXNE_RXFNE) {
      buf[got++] = (uint8_t)USART1->RDR;
      t0 = tb_ms();
    } else if ((tb_ms() - t0) > timeout_ms) {
      return got;
    }
  }
  return got;
}

// ---- Image conversion (must match the PC reference exactly) -----------------
static inline uint8_t rgb565_to_gray(uint16_t p) {
  uint32_t r5 = (p >> 11) & 0x1F;
  uint32_t g6 = (p >> 5) & 0x3F;
  uint32_t b5 = p & 0x1F;
  uint32_t r8 = (r5 << 3) | (r5 >> 2);
  uint32_t g8 = (g6 << 2) | (g6 >> 4);
  uint32_t b8 = (b5 << 3) | (b5 >> 2);
  return (uint8_t)((77 * r8 + 150 * g8 + 29 * b8) >> 8);
}

static void frame_to_int8(int w, int h) {
  for (int y = 0; y < IMG_H; ++y) {
    int sy = (int)((long)y * h / IMG_H);
    for (int x = 0; x < IMG_W; ++x) {
      int sx = (int)((long)x * w / IMG_W);
      uint8_t g = rgb565_to_gray(g_frame[sy * w + sx]);
      g_in[y * IMG_W + x] = (int8_t)((int)g - 128);
    }
  }
}

// D6 LED semantics: red = error, green = person, blue = no person.
static void led_result(int is_person) {
  Led_Red(0);
  Led_Green(is_person ? 1 : 0);
  Led_Blue(is_person ? 0 : 1);
}

static void led_error(void) {
  Led_Green(0);
  Led_Blue(0);
  Led_Red(1);
}

static void run_inference(void) {
  TfLiteTensor* in = g_interp->input(0);
  memcpy(in->data.int8, g_in, sizeof(g_in));
  if (g_interp->Invoke() != kTfLiteOk) {
    MicroPrintf("ERR invoke");
    led_error();
    return;
  }
  TfLiteTensor* out = g_interp->output(0);
  g_out[0] = out->data.int8[0];
  g_out[1] = out->data.int8[1];
  g_have_tensor = 1;
  float p0 = (float)((int)g_out[0] + 128) / 256.0f;  // not-person
  float p1 = (float)((int)g_out[1] + 128) / 256.0f;  // person
  led_result(g_out[1] > g_out[0]);
  MicroPrintf("RESULT %s %.3f %.3f", (g_out[1] > g_out[0]) ? "person" : "not_person",
              (double)p1, (double)p0);
}

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;
  HAL_Init();
  SystemClock_Config();
  HAL_InitTick(TICK_INT_PRIORITY);
  tb_init();
  Led_Init();  // D6 RGB LED: red = error, green = person, blue = no person
  tflite::InitializeTarget();
  USART1->BRR = (uint32_t)((120000000u + 115200u / 2u) / 115200u);

  MicroPrintf("=== H743 VWW person detection (UART RGB565) ===");
  if (!InitModel()) {
    MicroPrintf("ERR model init");
    led_error();
    while (1) {}
  }
  MicroPrintf("arena used = %d", (int)g_interp->arena_used_bytes());
  MicroPrintf("READY");

  for (;;) {
    uint8_t b = 0;
    if (!uart_read_byte(&b, 1000)) continue;

    if (b == 'P') { MicroPrintf("PONG"); continue; }
    if (b == 'D') {
      if (!g_have_tensor) { MicroPrintf("ERR no-tensor"); continue; }
      MicroPrintf("TENSOR_START");
      uart_write((const char*)g_in, sizeof(g_in));
      MicroPrintf("\r\nTENSOR_END");
      continue;
    }
    if (b != 0xA5) continue;

    uint8_t magic2 = 0;
    uint8_t hdr[4];
    if (!uart_read_byte(&magic2, 1000) || magic2 != 0x5A) { MicroPrintf("ERR magic"); led_error(); continue; }
    if (uart_read_buf(hdr, 4, 1000) != 4) { MicroPrintf("ERR hdr"); led_error(); continue; }
    int w = hdr[0] | (hdr[1] << 8);
    int h = hdr[2] | (hdr[3] << 8);
    if (w <= 0 || h <= 0 || w > MAX_SRC_W || h > MAX_SRC_H) {
      MicroPrintf("ERR size %d x %d", w, h);
      led_error();
      continue;
    }
    int bytes = w * h * 2;
    int got = uart_read_buf((uint8_t*)g_frame, bytes, 3000);
    if (got != bytes) { MicroPrintf("ERR frame %d/%d", got, bytes); led_error(); continue; }

    frame_to_int8(w, h);
    run_inference();
  }
  return 0;
}

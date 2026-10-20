// kws_mic_app.cc — H743 KWS with onboard mic (WM8978 + SAI1 + DMA) @44.1kHz
// Audio capture uses the Wildfire HAL BSP (bsp_wm8978/bsp_sai/i2c) VERBATIM
// (double-buffered circular DMA + TC interrupt) -> accumulate 1s -> our 44.1k
// log-mel DSP + int8 TFLM model -> UART output.

#include "stm32h7xx_hal.h"

#include <math.h>
#include <string.h>

extern "C" {
#include "bsp_wm8978.h"
#include "bsp_sai.h"
#include "i2c.h"
#include "led.h"
#include "key.h"
void SAI_GPIO_Config(void);
void SAIxA_Tx_Config(const uint16_t _usStandard, const uint16_t _usWordLen, const uint32_t _usAudioFreq);
void SAIxB_Rx_Config(const uint16_t _usStandard, const uint16_t _usWordLen, const uint32_t _usAudioFreq);
void SAIA_TX_DMA_Init(uint32_t buffer0, uint32_t buffer1, const uint32_t num);
void SAIB_RX_DMA_Init(uint32_t buffer0, uint32_t buffer1, const uint32_t num);
void SAI_Play_Start(void);
void SAI_Rec_Start(void);
extern DMA_HandleTypeDef h_txdma;
extern SAI_HandleTypeDef h_sai;
extern SAI_HandleTypeDef h_saib;
extern void uart_write(const char* s, int n);
void MusicPlayer_SAI_DMA_TX_Callback(void) {}
}

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "kws_assets.h"
#include "kws_dsp.h"
#include "timebase.h"

extern "C" {
extern const unsigned char g_kws2_44k_tinyconv_model_data[];
}

#ifndef SAI_I2S_STANDARD
#define SAI_I2S_STANDARD 0
#endif
#ifndef SAI_AUDIO_FREQUENCY_44K
#define SAI_AUDIO_FREQUENCY_44K 44100
#endif

#define RX_CHUNK 8192        // halfwords per half-buffer (Wildfire default for 44k)
#define MIC_TOTAL 88200      // 1s stereo @44.1kHz

static uint16_t g_rx0[RX_CHUNK] __attribute__((section(".sram")));
static uint16_t g_rx1[RX_CHUNK] __attribute__((section(".sram")));
static uint16_t g_buf[MIC_TOTAL] __attribute__((section(".sram")));
// DMA1 on STM32H7 CANNOT access DTCM (0x2000_xxxx); keep DMA buffers in AXI SRAM.
static uint32_t g_zero[32] __attribute__((section(".sram"))) = {0};

static volatile int g_pos = 0;
static volatile int g_ready = 0;
static volatile int g_rec_arm = 0;      // start accumulating at the next chunk boundary
static volatile int g_rec_active = 0;   // 1 while recording the 1 s window
static volatile int g_have_last = 0;    // g_mono holds a valid last window
static volatile int g_cb = 0;
static volatile int g_trigger_req = 0;  // debug: 'r' triggers one capture without KEY1
static volatile uint32_t g_loops = 0;   // diagnostics: main loop iterations
static volatile uint32_t g_rx_bytes = 0;  // diagnostics: UART bytes seen

static tflite::MicroInterpreter* g_interp = nullptr;
static uint8_t g_arena[16384];
static int8_t g_in_q[KWS_TFRAMES * KWS_NMEL];
static short g_mono[KWS_SR] __attribute__((section(".sram")));

void SysTick_Handler(void) { HAL_IncTick(); }

void SystemClock_Config(void) {
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  // H7 @480MHz requires VOS0 (boost) + LDO supply.
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

// DMA IRQ handlers -> Wildfire stream functions (which call HAL_DMA_IRQHandler
// on the BSP's h_txdma / h_rxdma and dispatch to the callbacks above/below).
extern "C" void DMA1_Stream2_IRQHandler(void) { SAI_TX_DMA_STREAM_IRQFUN(); }
extern "C" void DMA1_Stream3_IRQHandler(void) { SAI_RX_DMA_STREAM_IRQFUN(); }

// Called from the BSP when a half-buffer (RX_CHUNK halfwords) has been filled.
// One-shot capture: only accumulate while armed/active; start exactly at a
// chunk boundary so the 1 s window is contiguous.
extern "C" void MusicPlayer_SAI_DMA_RX_Callback(void) {
  g_cb++;
  if (g_rec_arm) {
    g_rec_arm = 0;
    g_rec_active = 1;
    g_pos = 0;
  }
  if (!g_rec_active) return;
  const uint16_t* src = (DMA1_Stream3->CR & (1u << 19)) ? g_rx0 : g_rx1;
  int n = RX_CHUNK;
  if (g_pos + n > MIC_TOTAL) n = MIC_TOTAL - g_pos;
  if (n > 0) {
    memcpy(&g_buf[g_pos], src, (size_t)n * 2);
    g_pos += n;
  }
  if (g_pos >= MIC_TOTAL) {
    g_rec_active = 0;      // recording finished
    Led_Red(0);            // red LED off: recording ended
    g_ready = 1;           // main loop runs inference next
  }
}

static bool InitModel() {
  const tflite::Model* m = tflite::GetModel(g_kws2_44k_tinyconv_model_data);
  if (m->version() != TFLITE_SCHEMA_VERSION) return false;
  static tflite::MicroMutableOpResolver<8> resolver;
  resolver.AddConv2D(); resolver.AddFullyConnected(); resolver.AddSoftmax();
  resolver.AddReshape(); resolver.AddPack(); resolver.AddShape(); resolver.AddStridedSlice();
  static tflite::MicroInterpreter interp(m, resolver, g_arena, sizeof(g_arena));
  g_interp = &interp;
  return g_interp->AllocateTensors() == kTfLiteOk;
}

static void InferMic() {
  // stereo interleaved -> left channel mono
  for (int i = 0; i < KWS_SR; ++i) g_mono[i] = (short)g_buf[i * 2];

  static float feat[KWS_TFRAMES * KWS_NMEL];
  kws_logmel(g_mono, KWS_SR, feat);
  for (int i = 0; i < KWS_TFRAMES * KWS_NMEL; ++i) {
    float v = (feat[i] - KWS_MEAN) / KWS_STD;
    int q = (int)lrintf(v / KWS_IN_SCALE) + KWS_IN_ZP;
    if (q > 127) q = 127;
    if (q < -128) q = -128;
    g_in_q[i] = (int8_t)q;
  }
  TfLiteTensor* in = g_interp->input(0);
  TfLiteTensor* out = g_interp->output(0);
  memcpy(in->data.int8, g_in_q, sizeof(g_in_q));
  if (g_interp->Invoke() != kTfLiteOk) { MicroPrintf("invoke fail\r\n"); return; }

  // argmax on int8 (monotonic with the dequantized probability)
  int best = 0;
  for (int c = 1; c < KWS_NCLASS; ++c) {
    if (out->data.int8[c] > out->data.int8[best]) best = c;
  }
  MicroPrintf("%s\r\n", KWS_CLASSES[best]);
}

int main(int argc, char* argv[]) {
  (void)argc; (void)argv;
  HAL_Init();
  SystemClock_Config();
  HAL_InitTick(TICK_INT_PRIORITY);   // re-arm SysTick for the new 480MHz clock
  tb_init();                          // DWT CYCCNT timebase (HAL_GetTick is 1 s/step)
  tflite::InitializeTarget();
  USART1->BRR = (uint32_t)((120000000u + 115200u / 2u) / 115200u);

  Led_Init();     // D6 RGB LED (red = recording)
  Key_Init();     // KEY1 (PA0, active high)

  MicroPrintf("\r\n=== H743 KWS mic (WM8978+SAI1+DMA @44.1k) ===\r\n");
  if (!InitModel()) { MicroPrintf("model init fail\r\n"); }
  MicroPrintf("arena used = %d\r\n", g_interp ? (int)g_interp->arena_used_bytes() : -1);

  I2cMaster_Init();
  if (!wm8978_Init()) { MicroPrintf("wm8978 init FAILED\r\n"); }
  else { MicroPrintf("wm8978 init OK\r\n"); }

  SAI_GPIO_Config();
  SAIxA_Tx_Config(SAI_I2S_STANDARD, 16, SAI_AUDIO_FREQUENCY_44K);   // master TX (clocks)
  SAIxB_Rx_Config(SAI_I2S_STANDARD, 16, SAI_AUDIO_FREQUENCY_44K);   // slave RX (ADC)

  SAIA_TX_DMA_Init((uint32_t)&g_zero[0], (uint32_t)&g_zero[16], 16);  // keep A clocking
  __HAL_DMA_DISABLE_IT(&h_txdma, DMA_IT_TC);
  SAIB_RX_DMA_Init((uint32_t)g_rx0, (uint32_t)g_rx1, RX_CHUNK);     // double-buffer RX

  wm8978_CfgAudioPath(MIC_LEFT_ON | MIC_RIGHT_ON | ADC_ON, EAR_LEFT_ON | EAR_RIGHT_ON);
  wm8978_SetMicGain(48);   // +24 dB PGA (boost adds +20 dB); 63 was too hot (noise + clipping)
  wm8978_CfgAudioIF(SAI_I2S_STANDARD, 16);

  MicroPrintf("A_CR1=0x%08X B_CR1=0x%08X hA.state=%d pll2rdy=%d\r\n",
              (unsigned)SAI1_Block_A->CR1, (unsigned)SAI1_Block_B->CR1,
              (int)h_sai.State, (int)((RCC->CR >> 27) & 1u));

  SAI_Rec_Start();     // SAI/DMA run continuously; we only accumulate on demand
  SAI_Play_Start();
  MicroPrintf("capturing...\r\n");
  MicroPrintf("ready: press KEY1 (red D6 = recording)\r\n");

  for (;;) {
    g_loops++;
    // --- diagnostics on UART: 'd' = dump last window, 'r' = remote trigger ---
    uint32_t uisr = USART1->ISR;
    if (uisr & USART_ISR_ORE) {          // overrun: clear it, otherwise RX stays dead
      USART1->ICR = USART_ICR_ORECF;
    }
    if (uisr & USART_ISR_RXNE_RXFNE) {
      char c = (char)USART1->RDR;
      g_rx_bytes++;
      if ((c == 'd' || c == 'D') && g_have_last) {
        MicroPrintf("DUMP_START\r\n");
        uart_write((const char*)g_mono, KWS_SR * 2);   // 44100 x int16 = 88200 bytes
        MicroPrintf("\r\nDUMP_END\r\n");
      } else if (c == 'r' || c == 'R') {
        g_trigger_req = 1;
      }
    }

    // --- a window just finished: infer and print the class token ---
    if (g_ready) {
      g_ready = 0;
      InferMic();          // red LED was turned off by the DMA callback
      g_have_last = 1;
      continue;
    }

    // --- KEY1: press (50 ms debounce) -> release -> 1.0 s wait -> record 1 s ---
    if (Key1_Poll() || g_trigger_req) {
      g_trigger_req = 0;
      tb_delay_ms(1000);   // 1.0 s wait, red LED stays off: get ready to speak
      __disable_irq();
      g_pos = 0;
      g_rec_arm = 1;       // start exactly at the next DMA chunk boundary
      __enable_irq();
      Led_Red(1);          // recording indicator ON
      while (!g_ready) { __NOP(); }   // wait for the 1 s window to complete
      g_ready = 0;
      InferMic();          // red LED was turned off by the DMA callback
      g_have_last = 1;
      continue;
    }

    tb_delay_ms(2);        // key poll period
  }
  return 0;
}

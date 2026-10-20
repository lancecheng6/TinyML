// h743_port.cc — TFLite Micro platform port for STM32H743 (H7Pro)
// Implements the three TFLM-required platform hooks: debug_log / micro_time / system_setup
//
// DebugLog -> routed to USART1(PA9) @115200 (see h743_uart.c) -> onboard CH340 -> host COMx
// This lets the board print hello_world_test's profiler / arena / "~~~ALL TESTS PASSED~~~".

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include "tensorflow/lite/micro/debug_log.h"
#include "tensorflow/lite/micro/micro_time.h"
#include "tensorflow/lite/micro/system_setup.h"

extern "C" void uart_init(void);
extern "C" void uart_write(const char* s, int n);

extern "C" void DebugLog(const char* format, va_list args) {
  char buf[256];
  int n = vsnprintf(buf, sizeof(buf), format, args);
  if (n > 0) {
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
    uart_write(buf, n);
  }
}

extern "C" int DebugVsnprintf(char* buffer, size_t buf_size, const char* format,
                              va_list vlist) {
  return vsnprintf(buffer, buf_size, format, vlist);
}

namespace tflite {

uint32_t ticks_per_second() { return 1000000; }

uint32_t GetCurrentTimeTicks() { return 0; }

void InitializeTarget() {
  uart_init();  // first call in main(): bring up UART early
}

}  // namespace tflite

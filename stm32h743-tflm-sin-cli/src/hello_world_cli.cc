/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

// h743_cli — derived from hello_world_test.cc with a rewritten main():
//   Read a real number x from UART (USART1 / PA9 TX, PA10 RX), run the TFLM float
//   model, and print x / y / sin(x) / error back over UART.
//   Usage (terminal on COMx, 115200 8N1): when "x> " appears, type a number and Enter.
// Note: the three test functions from the original hello_world_test.cc are kept
//   (they are no longer called by main); the original upstream file is untouched.

#include <math.h>

#include <cmath>
#include <cstdlib>

#include "stm32h743xx.h"

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/examples/hello_world/models/hello_world_float_model_data.h"
#include "tensorflow/lite/micro/examples/hello_world/models/hello_world_int8_model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler.h"
#include "tensorflow/lite/micro/recording_micro_interpreter.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#ifndef USART_ISR_RXNE_RXFNE
#define USART_ISR_RXNE_RXFNE USART_ISR_RXNE
#endif

// UART write (see h743_uart.c / h743_port.cc)
extern "C" void uart_write(const char* s, int n);
extern "C" void uart_putc(char c);

namespace {
using HelloWorldOpResolver = tflite::MicroMutableOpResolver<1>;

TfLiteStatus RegisterOps(HelloWorldOpResolver& op_resolver) {
  TF_LITE_ENSURE_STATUS(op_resolver.AddFullyConnected());
  return kTfLiteOk;
}

// ---- UART read: poll RXNE, with echo ----
char rxc() {
  while ((USART1->ISR & USART_ISR_RXNE_RXFNE) == 0u) {
  }
  return (char)(USART1->RDR & 0xFFu);
}

void read_line(char* buf, int n) {
  int i = 0;
  for (;;) {
    char c = rxc();
    uart_putc(c);  // echo back to the terminal
    if (c == '\r' || c == '\n') {
      if (i == 0) continue;  // ignore empty line / leading CR
      break;
    }
    if (i < n - 1) buf[i++] = c;
  }
  buf[i] = '\0';
  uart_write("\r\n", 2);
}
}  // namespace

TfLiteStatus ProfileMemoryAndLatency() {
  tflite::MicroProfiler profiler;
  HelloWorldOpResolver op_resolver;
  TF_LITE_ENSURE_STATUS(RegisterOps(op_resolver));

  // Arena size just a round number. The exact arena usage can be determined
  // using the RecordingMicroInterpreter.
  constexpr int kTensorArenaSize = 3000;
  uint8_t tensor_arena[kTensorArenaSize];
  constexpr int kNumResourceVariables = 24;

  tflite::RecordingMicroAllocator* allocator(
      tflite::RecordingMicroAllocator::Create(tensor_arena, kTensorArenaSize));
  tflite::RecordingMicroInterpreter interpreter(
      tflite::GetModel(g_hello_world_float_model_data), op_resolver, allocator,
      tflite::MicroResourceVariables::Create(allocator, kNumResourceVariables),
      &profiler);

  TF_LITE_ENSURE_STATUS(interpreter.AllocateTensors());
  TFLITE_CHECK_EQ(interpreter.inputs_size(), 1);
  interpreter.input(0)->data.f[0] = 1.f;
  TF_LITE_ENSURE_STATUS(interpreter.Invoke());

  MicroPrintf("");  // Print an empty new line
  profiler.LogTicksPerTagCsv();

  MicroPrintf("");  // Print an empty new line
  interpreter.GetMicroAllocator().PrintAllocations();
  return kTfLiteOk;
}

TfLiteStatus LoadFloatModelAndPerformInference() {
  const tflite::Model* model =
      ::tflite::GetModel(g_hello_world_float_model_data);
  TFLITE_CHECK_EQ(model->version(), TFLITE_SCHEMA_VERSION);

  HelloWorldOpResolver op_resolver;
  TF_LITE_ENSURE_STATUS(RegisterOps(op_resolver));

  // Arena size just a round number. The exact arena usage can be determined
  // using the RecordingMicroInterpreter.
  constexpr int kTensorArenaSize = 3000;
  uint8_t tensor_arena[kTensorArenaSize];

  tflite::MicroInterpreter interpreter(model, op_resolver, tensor_arena,
                                       kTensorArenaSize);
  TF_LITE_ENSURE_STATUS(interpreter.AllocateTensors());

  // Check if the predicted output is within a small range of the
  // expected output
  float epsilon = 0.05f;
  constexpr int kNumTestValues = 4;
  float golden_inputs[kNumTestValues] = {0.f, 1.f, 3.f, 5.f};

  for (int i = 0; i < kNumTestValues; ++i) {
    interpreter.input(0)->data.f[0] = golden_inputs[i];
    TF_LITE_ENSURE_STATUS(interpreter.Invoke());
    float y_pred = interpreter.output(0)->data.f[0];
    TFLITE_CHECK_LE(abs(sin(golden_inputs[i]) - y_pred), epsilon);
  }

  return kTfLiteOk;
}

TfLiteStatus LoadQuantModelAndPerformInference() {
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model =
      ::tflite::GetModel(g_hello_world_int8_model_data);
  TFLITE_CHECK_EQ(model->version(), TFLITE_SCHEMA_VERSION);

  HelloWorldOpResolver op_resolver;
  TF_LITE_ENSURE_STATUS(RegisterOps(op_resolver));

  // Arena size just a round number. The exact arena usage can be determined
  // using the RecordingMicroInterpreter.
  constexpr int kTensorArenaSize = 3000;
  uint8_t tensor_arena[kTensorArenaSize];

  tflite::MicroInterpreter interpreter(model, op_resolver, tensor_arena,
                                       kTensorArenaSize);

  TF_LITE_ENSURE_STATUS(interpreter.AllocateTensors());

  TfLiteTensor* input = interpreter.input(0);
  TFLITE_CHECK_NE(input, nullptr);

  TfLiteTensor* output = interpreter.output(0);
  TFLITE_CHECK_NE(output, nullptr);

  float output_scale = output->params.scale;
  int output_zero_point = output->params.zero_point;

  // Check if the predicted output is within a small range of the
  // expected output
  float epsilon = 0.05;

  constexpr int kNumTestValues = 4;
  float golden_inputs_float[kNumTestValues] = {0.77, 1.57, 2.3, 3.14};

  // The int8 values are calculated using the following formula
  // (golden_inputs_float[i] / input->params.scale + input->params.zero_point)
  int8_t golden_inputs_int8[kNumTestValues] = {-96, -63, -34, 0};

  for (int i = 0; i < kNumTestValues; ++i) {
    input->data.int8[0] = golden_inputs_int8[i];
    TF_LITE_ENSURE_STATUS(interpreter.Invoke());
    float y_pred = (output->data.int8[0] - output_zero_point) * output_scale;
    TFLITE_CHECK_LE(abs(sin(golden_inputs_float[i]) - y_pred), epsilon);
  }

  return kTfLiteOk;
}

// ===== Interactive CLI: UART in x -> model inference -> print x / y / sin(x) / err =====
int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  tflite::InitializeTarget();  // h743_port.cc -> uart_init()

  const tflite::Model* model = ::tflite::GetModel(g_hello_world_float_model_data);
  TFLITE_CHECK_EQ(model->version(), TFLITE_SCHEMA_VERSION);

  HelloWorldOpResolver op_resolver;
  TF_LITE_ENSURE_STATUS(RegisterOps(op_resolver));

  constexpr int kTensorArenaSize = 3000;
  static uint8_t tensor_arena[kTensorArenaSize];

  tflite::MicroInterpreter interpreter(model, op_resolver, tensor_arena,
                                       kTensorArenaSize);
  TF_LITE_ENSURE_STATUS(interpreter.AllocateTensors());

  MicroPrintf("TFLM hello_world CLI (float). arena=%d bytes\r\n",
              (int)interpreter.arena_used_bytes());
  MicroPrintf("Type x then Enter (e.g. 0, 1.57, 3.14):\r\n");

  char line[32];
  for (;;) {
    uart_write("x> ", 3);
    read_line(line, sizeof(line));

    float x = strtof(line, nullptr);
    interpreter.input(0)->data.f[0] = x;
    TF_LITE_ENSURE_STATUS(interpreter.Invoke());

    float y = interpreter.output(0)->data.f[0];
    float t = sinf(x);
    MicroPrintf("x=%.4f  y=%.5f  sin(x)=%.5f  err=%.5f\r\n", (double)x,
                (double)y, (double)t, (double)fabsf(t - y));
  }
  return kTfLiteOk;
}

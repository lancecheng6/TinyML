#!/usr/bin/env bash
# build_h743_kws_mic.sh — Build the on-device keyword-spotting (yes/no) firmware
#                         for STM32H743 (Wildfire H7Pro core board).
#
# Output: out/kws_mic.elf / .bin / .hex
#
# Toolchain: arm-none-eabi-gcc (Cortex-M7)
#   - Install (Debian/Ubuntu/WSL):  sudo apt install gcc-arm-none-eabi
#   - Or point to a custom toolchain: ARM_GCC=/path/to/bin bash build_h743_kws_mic.sh
#
# Everything else is bundled: TFLM static lib + headers, ST HAL subset,
# Wildfire BSP subset (WM8978/SAI/I2C/delay), startup + linker script,
# and the trained int8 model as C arrays (assets/).

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
mkdir -p "$OUT"

# --- Toolchain ---
if [ -n "$ARM_GCC" ]; then
  CC="$ARM_GCC/arm-none-eabi-gcc"
  CXX="$ARM_GCC/arm-none-eabi-g++"
  OBJCOPY="$ARM_GCC/arm-none-eabi-objcopy"
  SIZE="$ARM_GCC/arm-none-eabi-size"
else
  CC=arm-none-eabi-gcc
  CXX=arm-none-eabi-g++
  OBJCOPY=arm-none-eabi-objcopy
  SIZE=arm-none-eabi-size
fi

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "ERROR: '$CC' not found."
  echo "  sudo apt install gcc-arm-none-eabi"
  echo "  or: ARM_GCC=/path/to/bin bash build_h743_kws_mic.sh"
  exit 1
fi

LIB="$ROOT/lib/libtensorflow-microlite.a"
ASSETS="$ROOT/assets"
W="$ROOT/third_party/wildfire_bsp"

CPU="-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
DEF="-DTF_LITE_STATIC_MEMORY -DTF_LITE_MCU_DEBUG_LOG -DCPU_M7=1 -DUSE_HAL_DRIVER -DSTM32H743xx -funsigned-char -fno-rtti -fno-exceptions -fno-threadsafe-statics"
OPT="-Os -ffunction-sections -fdata-sections"

INC="-I$ROOT/src -I$ASSETS \
 -I$ROOT/include -I$ROOT/include/gemmlowp -I$ROOT/include/ruy -I$ROOT/include/kissfft \
 -I$ROOT/include/cmsis_nn -I$ROOT/include/cmsis_core -I$ROOT/include/stm32h7xx \
 -I$ROOT/hal/Inc \
 -I$W -I$W/wm8978 -I$W/sai -I$W/i2c -I$W/delay -I$W/usart"

echo "=== 1) startup / system ==="
"$CC" $CPU $OPT $DEF $INC -c "$ROOT/bsp/startup_stm32h743xx.s" -o "$OUT/startup.o"
"$CC" $CPU $OPT $DEF $INC -c "$ROOT/bsp/system_stm32h7xx.c"   -o "$OUT/system.o"

echo "=== 2) ST HAL (subset) ==="
for f in stm32h7xx_hal stm32h7xx_hal_cortex stm32h7xx_hal_rcc stm32h7xx_hal_rcc_ex \
         stm32h7xx_hal_gpio stm32h7xx_hal_i2c stm32h7xx_hal_i2c_ex stm32h7xx_hal_dma \
         stm32h7xx_hal_dma_ex stm32h7xx_hal_sai stm32h7xx_hal_sai_ex stm32h7xx_hal_pwr \
         stm32h7xx_hal_pwr_ex stm32h7xx_hal_flash stm32h7xx_hal_flash_ex \
         stm32h7xx_hal_exti stm32h7xx_hal_tim stm32h7xx_hal_tim_ex stm32h7xx_hal_mdma; do
  "$CC" $CPU $OPT $DEF $INC -c "$ROOT/hal/Src/$f.c" -o "$OUT/$f.o"
done

echo "=== 3) Wildfire BSP (WM8978 / SAI / I2C / delay) ==="
"$CC" $CPU $OPT $DEF $INC -c "$W/wm8978/bsp_wm8978.c" -o "$OUT/bsp_wm8978.o"
"$CC" $CPU $OPT $DEF $INC -c "$W/sai/bsp_sai.c"       -o "$OUT/bsp_sai.o"
"$CC" $CPU $OPT $DEF $INC -c "$W/i2c/i2c.c"           -o "$OUT/i2c.o"
"$CC" $CPU $OPT $DEF $INC -c "$W/delay/core_delay.c"  -o "$OUT/core_delay.o"

echo "=== 4) TFLM port / UART ==="
"$CC"  $CPU $OPT -DSTM32H743xx -DCPU_M7=1 -funsigned-char $INC -c "$ROOT/src/h743_uart.c" -o "$OUT/uart.o"
"$CXX" $CPU $OPT $DEF $INC -c "$ROOT/src/h743_port.cc" -o "$OUT/port.o"

echo "=== 5) KWS app (DSP + model + LED/KEY) ==="
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/src/fft.c"      -o "$OUT/fft.o"
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/src/kws_dsp.c"  -o "$OUT/kws_dsp.o"
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/src/led.c"      -o "$OUT/led.o"
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/src/key.c"      -o "$OUT/key.o"
"$CC"  $CPU $OPT $DEF $INC -c "$ASSETS/kws2_44k_tinyconv_model_data.cc" -o "$OUT/model.o"
"$CXX" $CPU $OPT $DEF $INC -c "$ROOT/src/kws_mic_app.cc" -o "$OUT/kws_mic_app.o"

echo "=== 6) link ==="
"$CXX" $CPU -T "$ROOT/bsp/stm32h743.ld" -Wl,--gc-sections -Wl,-Map="$OUT/kws_mic.map" \
  --specs=nosys.specs \
  "$OUT"/*.o "$LIB" -lm -o "$OUT/kws_mic.elf"

"$OBJCOPY" -O binary "$OUT/kws_mic.elf" "$OUT/kws_mic.bin"
"$OBJCOPY" -O ihex   "$OUT/kws_mic.elf" "$OUT/kws_mic.hex"

echo "=== size ==="
"$SIZE" "$OUT/kws_mic.elf"
echo "DONE: $OUT/kws_mic.elf"

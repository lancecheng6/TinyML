#!/usr/bin/env bash
# build_h743_vww_uart.sh — STM32H743 VWW (person / not-person) over UART (RGB565)
#
# Output: out/vww_uart.elf / .bin / .hex
#
# Toolchain: arm-none-eabi-gcc (Cortex-M7)
#   sudo apt install gcc-arm-none-eabi
#   or: ARM_GCC=/path/to/bin bash build_h743_vww_uart.sh
#
# Everything else is bundled: TFLM static lib + headers, ST HAL subset,
# startup + linker script, and the person_detect model as a C array.

set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/out"
mkdir -p "$OUT"

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
  echo "  or: ARM_GCC=/path/to/bin bash build_h743_vww_uart.sh"
  exit 1
fi

LIB="$ROOT/lib/libtensorflow-microlite.a"
CPU="-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
DEF="-DTF_LITE_STATIC_MEMORY -DTF_LITE_MCU_DEBUG_LOG -DCPU_M7=1 -DUSE_HAL_DRIVER -DSTM32H743xx -funsigned-char -fno-rtti -fno-exceptions -fno-threadsafe-statics"
OPT="-Os -ffunction-sections -fdata-sections"
INC="-I$ROOT/src -I$ROOT/assets -I$ROOT/include -I$ROOT/include/gemmlowp \
 -I$ROOT/include/kissfft -I$ROOT/include/cmsis_core -I$ROOT/include/stm32h7xx \
 -I$ROOT/hal/Inc"

echo "=== startup / system ==="
"$CC" $CPU $OPT $DEF $INC -c "$ROOT/bsp/startup_stm32h743xx.s" -o "$OUT/startup.o"
"$CC" $CPU $OPT $DEF $INC -c "$ROOT/bsp/system_stm32h7xx.c"   -o "$OUT/system.o"

echo "=== ST HAL (subset) ==="
for f in stm32h7xx_hal stm32h7xx_hal_cortex stm32h7xx_hal_rcc stm32h7xx_hal_rcc_ex \
         stm32h7xx_hal_gpio stm32h7xx_hal_i2c stm32h7xx_hal_i2c_ex stm32h7xx_hal_dma \
         stm32h7xx_hal_dma_ex stm32h7xx_hal_sai stm32h7xx_hal_sai_ex stm32h7xx_hal_pwr \
         stm32h7xx_hal_pwr_ex stm32h7xx_hal_flash stm32h7xx_hal_flash_ex \
         stm32h7xx_hal_exti stm32h7xx_hal_tim stm32h7xx_hal_tim_ex stm32h7xx_hal_mdma; do
  "$CC" $CPU $OPT $DEF $INC -c "$ROOT/hal/Src/$f.c" -o "$OUT/$f.o"
done

echo "=== TFLM port / UART ==="
"$CC"  $CPU $OPT -DSTM32H743xx -DCPU_M7=1 -funsigned-char $INC -c "$ROOT/src/h743_uart.c" -o "$OUT/uart.o"
"$CXX" $CPU $OPT $DEF $INC -c "$ROOT/src/h743_port.cc" -o "$OUT/port.o"

echo "=== model + app ==="
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/assets/person_detect_model_data.cc" -o "$OUT/model.o"
"$CC"  $CPU $OPT $DEF $INC -c "$ROOT/src/led.c" -o "$OUT/led.o"
"$CXX" $CPU $OPT $DEF $INC -c "$ROOT/src/person_uart_app.cc" -o "$OUT/app.o"

echo "=== link ==="
"$CXX" $CPU -T "$ROOT/bsp/stm32h743.ld" -Wl,--gc-sections -Wl,-Map="$OUT/vww_uart.map" \
  --specs=nosys.specs \
  "$OUT"/*.o "$LIB" -lm -o "$OUT/vww_uart.elf"

"$OBJCOPY" -O binary "$OUT/vww_uart.elf" "$OUT/vww_uart.bin"
"$OBJCOPY" -O ihex   "$OUT/vww_uart.elf" "$OUT/vww_uart.hex"

echo "=== size ==="
"$SIZE" "$OUT/vww_uart.elf"
echo "DONE: $OUT/vww_uart.elf"

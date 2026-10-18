#!/usr/bin/env bash
# build_h743_cli.sh — Build the TFLM "sin(x)" interactive CLI image for STM32H743 (H7Pro)
#
# Output: out/hello_world_h743_cli.elf / .bin / .hex
#
# Toolchain: requires arm-none-eabi-gcc (Cortex-M cross compiler)
#   - If installed and on PATH: just run this script
#   - Or specify a custom toolchain:  ARM_GCC=/path/to/arm-none-eabi/bin bash build_h743_cli.sh
#   - Install (Debian/Ubuntu/WSL):    sudo apt install gcc-arm-none-eabi
#
# Deps (all bundled): lib/libtensorflow-microlite.a (cortex-m7+fp + CMSIS-NN),
#   include/ header tree, bsp/ startup & linker, include/.../models/ model C arrays.

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
  echo "ERROR: '$CC' not found"
  echo "Please install an arm-none-eabi toolchain, e.g.:"
  echo "  sudo apt install gcc-arm-none-eabi      # Debian/Ubuntu/WSL"
  echo "or download the ARM GNU Toolchain and set ARM_GCC=/path/to/bin, then rerun."
  exit 1
fi

LIB="$ROOT/lib/libtensorflow-microlite.a"
GEN="$ROOT/include"
APP="$ROOT/src/hello_world_cli.cc"
STARTUP="$ROOT/bsp/startup_stm32h743xx.s"
SYSTEM="$ROOT/bsp/system_stm32h7xx.c"
LD="$ROOT/bsp/stm32h743.ld"

INC="-I$ROOT/include -I$ROOT/include/gemmlowp -I$ROOT/include/ruy -I$ROOT/include/kissfft -I$ROOT/include/cmsis_nn -I$ROOT/include/cmsis_core -I$ROOT/include/stm32h7xx"
CPU="-mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
DEF="-DTF_LITE_STATIC_MEMORY -DTF_LITE_MCU_DEBUG_LOG -DCPU_M7=1 -funsigned-char -fno-rtti -fno-exceptions -fno-threadsafe-statics"
OPT="-Os -ffunction-sections -fdata-sections"

echo "=== 1) Compile startup / system ==="
"$CC" $CPU $OPT $DEF $INC -c "$STARTUP" -o "$OUT/startup.o"
"$CC" $CPU $OPT $DEF $INC -DSTM32H743xx -c "$SYSTEM" -o "$OUT/system.o"

echo "=== 2) Compile port layer / UART / app / model C arrays ==="
"$CC"  $CPU $OPT -DSTM32H743xx -DCPU_M7=1 -funsigned-char $INC -c "$ROOT/src/h743_uart.c" -o "$OUT/uart.o"
"$CXX" $CPU $OPT $DEF $INC -c "$ROOT/src/h743_port.cc" -o "$OUT/port.o"
"$CXX" $CPU $OPT $DEF $INC -c "$APP" -o "$OUT/app.o"
"$CC"  $CPU $OPT $DEF $INC -c "$GEN/tensorflow/lite/micro/examples/hello_world/models/hello_world_float_model_data.cc" -o "$OUT/mfloat.o"
"$CC"  $CPU $OPT $DEF $INC -c "$GEN/tensorflow/lite/micro/examples/hello_world/models/hello_world_int8_model_data.cc"  -o "$OUT/mint8.o"

echo "=== 3) Link ==="
"$CXX" $CPU -T "$LD" -Wl,--gc-sections -Wl,-Map="$OUT/hello_world_h743_cli.map" \
  --specs=nosys.specs \
  "$OUT/startup.o" "$OUT/system.o" "$OUT/uart.o" "$OUT/port.o" "$OUT/app.o" \
  "$OUT/mfloat.o" "$OUT/mint8.o" \
  "$LIB" -lm -o "$OUT/hello_world_h743_cli.elf"

echo "=== 4) Size and .bin/.hex ==="
"$SIZE" "$OUT/hello_world_h743_cli.elf"
"$OBJCOPY" -O binary "$OUT/hello_world_h743_cli.elf" "$OUT/hello_world_h743_cli.bin"
"$OBJCOPY" -O ihex   "$OUT/hello_world_h743_cli.elf" "$OUT/hello_world_h743_cli.hex"
echo "DONE: $OUT/hello_world_h743_cli.elf"

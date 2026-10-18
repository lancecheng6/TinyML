# STM32H743 × TensorFlow Lite Micro — sin(x) Interactive CLI

An end-to-end TinyML example: train a `sin(x)` regression model on a PC, quantize it,
cross-compile **TensorFlow Lite Micro (TFLM)** for an **STM32H743 (Cortex-M7)**, then
interact over **UART** — type `x` and read the model's `sin(x)` output live.

## Highlights
- Pure C/C++, **no OS, no dynamic memory** (TFLM static Tensor Arena)
- **CMSIS-NN** accelerated int8 kernels (`cortex-m7+fp`)
- **UART interactive CLI** on USART1 (PA9=TX / PA10=RX): type `x`, get `x / y / sin(x) / err`
- A **prebuilt image** is provided in `prebuilt/` (flash-ready)

## Hardware
| Item | Detail |
|------|--------|
| Board | Wildfire H7Pro — STM32H743XIH6 (Cortex-M7 @480 MHz, 2 MB Flash, 1 MB RAM) |
| Debug probe | fireDAP (CMSIS-DAP) or ST-LINK |
| Serial | onboard CH340 → host `COMx`, **115200 8N1** |

## Prerequisites
- **arm-none-eabi-gcc** (Cortex-M cross compiler)
  - `sudo apt install gcc-arm-none-eabi` (Debian / Ubuntu / WSL)
  - or ARM GNU Toolchain 14.3, then pass `ARM_GCC=/path/to/bin`
- **Flashing**: `pyocd` (`pip install pyocd`) or STM32CubeProgrammer / Keil
- **Terminal**: any serial terminal (MobaXterm / PuTTY / picocom / minicom)

## Build
```bash
bash build_h743_cli.sh
# with a custom toolchain location:
ARM_GCC=/path/to/arm-none-eabi/bin bash build_h743_cli.sh
```
Output: `out/hello_world_h743_cli.elf` / `.bin` / `.hex`

## Flash
```bash
pyocd flash --target stm32h743xx out/hello_world_h743_cli.elf
# or use the bundled image directly:
pyocd flash --target stm32h743xx prebuilt/hello_world_h743_cli.elf
```

## Usage
1. Open the serial port (`COMx`, 115200 8N1, Flow control = None)
2. Reset the board → the banner appears (`TFLM hello_world CLI (float). arena=… bytes`)
3. At the `x> ` prompt, type a number (e.g. `1.57`) and press **Enter**
4. The board replies, e.g.:
```
x=1.5700  y=0.99998  sin(x)=1.00000  err=0.00002
```
> You can keep typing; the model is not reloaded. Inputs outside `[0, 2π]` are **extrapolation**
> and the result is not guaranteed.

## Project layout
```
.
├── build_h743_cli.sh      build script (relative paths; uses PATH or ARM_GCC toolchain)
├── src/
│   ├── hello_world_cli.cc interactive CLI (UART in x -> inference -> print result)
│   ├── h743_port.cc       TFLM platform port (debug_log / micro_time / system_setup)
│   └── h743_uart.c        bare-metal USART1 driver (PA9/PA10, 115200)
├── include/               header tree (TFLM / signal / gemmlowp / ruy / kissfft / flatbuffers / CMSIS / model)
│   └── tensorflow/lite/micro/examples/hello_world/models/hello_world_{float,int8}_model_data.{cc,h}
├── bsp/                   startup & linker (startup_stm32h743xx.s, system_stm32h7xx.c, stm32h743.ld)
├── lib/libtensorflow-microlite.a   prebuilt TFLM static library (cortex-m7+fp + CMSIS-NN)
├── prebuilt/hello_world_h743_cli.{elf,bin}   prebuilt image
└── docs/                  on-device verification tables
```

## Model
- Task: learn `sin(x)` for `x ∈ [0, 2π]` (regression)
- Architecture: `Dense(16,relu) → Dense(16,relu) → Dense(1)` (~321 parameters)
- Size: float **3,252 B** / int8 **3,464 B** (post-training quantization, representative dataset)
- On-device result (100 random points, `x∈[0,2π]`): mean error ≈ 0.017, 94/100 within 0.05
  (see `docs/verification_100pts.md`)

## Memory layout (`stm32h743.ld`)
- Flash `0x08000000`: vector table + code + model
- DTCM `0x20000000`: `.data` / `.bss` (incl. Tensor Arena, stack)
- AXI SRAM `0x24000000`: reserved `.sram` section for DMA (unused in this example)

## License
Third-party components bundled here are attributed in [`NOTICE`](NOTICE); the primary license
is **Apache-2.0** (see [`LICENSE`](LICENSE)). `src/hello_world_cli.cc` is derived from the
TensorFlow Lite Micro official example (Apache-2.0); the model is trained by this project.

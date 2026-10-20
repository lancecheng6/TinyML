# On-device Keyword Spotting (yes / no) with the STM32H743 Onboard Microphone

Real-time keyword spotting (KWS) on an **STM32H743 (Wildfire H7Pro core board)** using the
**onboard microphone (WM8978)**:

press **KEY1** → the **red D6 LED turns on for 1 second while recording** → TFLite Micro
(int8) inference → **UART prints `yes` / `no` / `silence` / `unknown`**.

This repository is **self-contained**: it ships the trained int8 model (as C arrays), the
TFLM static library and headers, a subset of the ST HAL, a subset of the Wildfire BSP, and
the startup/linker files. **All you need is `arm-none-eabi-gcc` to build the `.elf`.**

---

## 1. Results (measured)

| Item | Value |
|------|-------|
| Model | tiny_conv (4 classes: silence / unknown / no / yes), **int8 145,928 B** |
| Features | 44.1 kHz, frame 1323/882 (~30/20 ms), FFT 2048, 40 mel bands (125–7500 Hz), 49 frames |
| Tensor arena | **13,304 B** |
| Clean test set | **1.0000 (33/33)** |
| Live test (KEY1) | **yes / no / silence, 3 runs each — all correct** |
| Target board | Wildfire H7Pro (STM32H743XIH6, HSE 25 MHz, 2 MB Flash, 1 MB RAM) |

---

## 2. System Architecture

```
                 I2C4 (PB8/PB9, 0x1A)
   KEY1 (PA0) ──────────────┐
                            ▼
   Onboard mic ──► WM8978 ADC ──SAI1_B (RX, slave)──► DMA1_Stream3
                                                        │  (double buffer, 8192 halfwords)
                                                        ▼
                                        g_buf[88200] (1 s stereo interleaved, AXI SRAM)
                                                        │
                                      left channel → g_mono[44100]
                                                        │
                              log-mel (44.1k / 49×40) → (x−mean)/std → int8
                                                        │
                                              TFLM tiny_conv (int8)
                                                        │
                                                        ▼
                                        USART1 (PA9/PA10) 115200 → `yes` / `no` / …
```

- **SAI1**: PE2=MCLK_A, PE4=FS_A, PE5=SCK_A, PE6=SD_A, PE3=SD_B (AF6); Block A is master TX
  (generates the clocks), Block B is slave RX.
- **Clocks**: PLL1 → SYSCLK 480 MHz; PLL2 (M=25, N=344, P=2) → 172 MHz → SAI MCLK.
  The actual sample rate is **44,800 Hz** (MCKDIV=15; +1.6% vs 44.1 kHz, which does not affect
  recognition).
- **DMA buffers must live in AXI SRAM** (DMA1 on STM32H7 cannot access DTCM).

### Pin Map

| Signal | Pin | Notes |
|--------|-----|-------|
| D6 R / G / B | **PB0 / PB1 / PA3** | RGB LED, **active low** (this project uses red = recording) |
| KEY1 | **PA0** | Input, **high when pressed** (external pull-down on the board) |
| WM8978 SCL / SDA | PB8 / PB9 | I²C4, 7-bit address `0x1A` |
| SAI1 | PE2 / PE3 / PE4 / PE5 / PE6 | MCLK_A / SD_B / FS_A / SCK_A / SD_A (AF6) |
| USART1 | PA9 / PA10 | TX / RX, 115200 8N1 (onboard CH340) |
| fireDAP | SWD | CMSIS-DAP flashing/debugging |

---

## 3. Repository Layout

```
stm32h743-kws-mic/
├── build_h743_kws_mic.sh      # one-shot build (produces out/kws_mic.elf/.bin/.hex)
├── src/                       # project source code
│   ├── kws_mic_app.cc         #   main app: state machine, inference, UART
│   ├── kws_dsp.c/.h           #   44.1k log-mel features (identical to the PC training pipeline)
│   ├── fft.c/.h               #   generic N-point radix-2 FFT
│   ├── led.c/.h               #   D6 RGB LED (PB0/PB1/PA3, active low)
│   ├── key.c/.h               #   KEY1 (PA0, 50 ms debounce, triggers on release)
│   ├── timebase.h             #   DWT CYCCNT millisecond timebase
│   ├── h743_port.cc           #   TFLM platform port (DebugLog → USART1)
│   └── h743_uart.c            #   bare-metal USART1 driver
├── assets/                    # trained model (C arrays) + feature parameters
│   ├── kws_assets.h           #   hann / mel / mean-std / quantization params / class names
│   └── kws2_44k_tinyconv_model_data.cc/.h
├── bsp/                       # startup / system / linker script
├── hal/                       # ST HAL driver subset (only what the build needs)
├── third_party/wildfire_bsp/  # Wildfire BSP subset (WM8978 / SAI / I2C / delay)
├── include/                   # TFLM / CMSIS / gemmlowp / kissfft / flatbuffers headers (trimmed)
├── lib/libtensorflow-microlite.a   # prebuilt cortex-m7+fp + CMSIS-NN library
└── prebuilt/                  # verified kws_mic.elf/.bin/.hex (ready to flash)
```

---

## 4. Build

Requires `arm-none-eabi-gcc` (Cortex-M7). On Debian/Ubuntu/WSL:

```bash
sudo apt install gcc-arm-none-eabi
```

Build:

```bash
bash build_h743_kws_mic.sh
# outputs: out/kws_mic.elf  out/kws_mic.bin  out/kws_mic.hex
```

If the toolchain is not on `PATH` (e.g. the official ARM tarball):

```bash
ARM_GCC=/path/to/gcc-arm-none-eabi/bin bash build_h743_kws_mic.sh
```

---

## 5. Flashing

Using **pyOCD** (a full chip erase is recommended to avoid inconsistent images after
partial flashing):

```bash
pyocd erase  -t stm32h743xx --chip
pyocd flash  -t stm32h743xx out/kws_mic.elf
```

You can also use STM32CubeProgrammer or OpenOCD to flash `out/kws_mic.hex` / `.bin`
(the `.bin` load address is `0x08000000`).

> If UART prints nothing after flashing while SWD shows the core running, erase the whole
> chip and flash again.

---

## 6. Usage

1. Open the serial port at 115200 8N1 (onboard CH340 → `COMx`). You should see:
   ```
   === H743 KWS mic (WM8978+SAI1+DMA @44.1k) ===
   arena used = 13304
   wm8978 init OK
   capturing...
   ready: press KEY1 (red D6 = recording)
   ```
2. **Press KEY1 once** (hold for about 0.3 s, then release).
3. About **1 second** after releasing, the D6 **red LED turns on for 1 second** — during that
   window, say **yes** or **no** once, about 5 cm from the microphone.
4. After the LED turns off, inference runs and UART prints **one line**:

   | Output | Meaning |
   |--------|---------|
   | `yes` | detected "yes" |
   | `no` | detected "no" |
   | `silence` | no speech |
   | `unknown` | other sounds |

> Each capture requires a new KEY1 press (one press = one recording = one result).

### Diagnostic commands (optional)

| Key (serial port) | Function |
|-------------------|----------|
| `r` | remote trigger (same flow, without the physical button) |
| `d` | dump the most recent 1-second audio window as binary (`DUMP_START` … 88200 bytes … `DUMP_END`) |

---

## 7. License and Third-Party Components

- Project code: **Apache License 2.0** (see `LICENSE`).
- Third-party components (TFLM, CMSIS, CMSIS-NN, gemmlowp, ruy, kissfft, FlatBuffers, ST HAL,
  ST device headers/startup/system, and the Wildfire BSP subset): see **`NOTICE`** for sources
  and licenses. The Wildfire BSP is included only as the minimal subset required to build this
  project; copyright belongs to the original author (Wildfire / fire).

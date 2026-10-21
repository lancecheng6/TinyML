# On-Device Person Detection on STM32H743 (Visual Wake Words over UART)

Binary **person / not-person** classification running on an **STM32H743** (Wildfire H7Pro).

No camera is required: the PC converts an image to **160×120 RGB565** and streams it over
**UART**; the board performs resizing, grayscale conversion, quantization and **TFLite Micro**
inference, then reports the result on **UART** and the **D6 RGB LED**.

> Images are streamed over UART (no camera), so the complete image pre-processing and TFLM
> inference path can be validated directly. To add a camera later, only the image source needs
> to change (UART → DCMI); the rest of the pipeline stays the same.

---

## 1. Model

| Item | Details |
|------|---------|
| Model | TFLM's bundled **`person_detect.tflite`** (**no training required**) |
| Task | 2 classes: `0 = not-person`, `1 = person` |
| Input | **96×96×1 grayscale, int8** (scale 1/127.5, zero_point −1) |
| Output | int8, 2 classes (scale 1/256, zp −128) |
| Model file | 300,568 B (converted to a C array: `assets/person_detect_model_data.cc`) |
| Tensor arena | 136 KB declared, **82,308 B used** (placed in AXI SRAM) |
| Operators | Conv2D / DepthwiseConv2D / AveragePool2D / Reshape / Softmax (int8) |

> To use your own model: Keras → int8 `.tflite` → C array (same file name/symbol), then rebuild.

---

## 2. Architecture

```
PC (host)                                  H743 (target)
---------                                  -------------
load JPG/PNG
  -> resize to 160x120
  -> convert to RGB565 (LE)
  -> send over UART  ------------------->  receive 160x120 RGB565 (polling)
                                             -> nearest-neighbour 96x96
                                             -> grayscale (77/150/29)
                                             -> int8 (gray - 128)
                                             -> TFLM person_detect
  <- RESULT <class> <p_person> <p_not>  --  RESULT over UART + D6 LED
```

- The PC only acts as a **camera substitute** (resize + RGB565); inference runs on the board.
- The board's pre-processing is **byte-identical** to the PC reference implementation
  (verify with `pc/check_parity.py`).

### Hardware and pins

| Signal | Pin | Notes |
|--------|-----|-------|
| USART1 TX / RX | **PA9 / PA10** | 115200 8N1 (onboard CH340 → `COMx`) |
| D6 R / G / B | **PB0 / PB1 / PA3** | RGB LED, **active low** |
| fireDAP | SWD | flashing/debugging |

### LED indication

| Result | LED |
|--------|-----|
| `person` | **green** |
| `not_person` | **blue** |
| error (protocol/inference) | **red** |

> The LED keeps showing the last result until the next result or error.

---

## 3. Repository Layout

```
stm32h743-vww-uart/
├── build_h743_vww_uart.sh    # one-shot build
├── src/
│   ├── person_uart_app.cc    #   main app: UART protocol, pre-processing, TFLM, LED
│   ├── led.c/.h              #   D6 RGB LED
│   ├── timebase.h            #   DWT CYCCNT millisecond timebase
│   ├── h743_port.cc          #   TFLM platform port (DebugLog → USART1)
│   └── h743_uart.c           #   bare-metal USART1 driver
├── assets/                   # model C array
├── bsp/                      # startup / system / linker script
├── hal/                      # ST HAL subset (Inc/Src)
├── include/                  # TFLM / CMSIS / gemmlowp / kissfft / flatbuffers headers
├── lib/libtensorflow-microlite.a
├── pc/                       # host-side tools
│   ├── prep_images.py        #   JPG/PNG → 160×120 RGB565 (+ expected tensor)
│   ├── send_images.py        #   Windows: send frames, read results, dump tensors
│   └── check_parity.py       #   PC vs board tensor comparison
└── prebuilt/                 # ready-to-flash vww_uart.elf/.bin/.hex
```

---

## 4. Build

Requires `arm-none-eabi-gcc` (Cortex-M7).

```bash
sudo apt install gcc-arm-none-eabi      # Debian/Ubuntu/WSL
bash build_h743_vww_uart.sh
# outputs: out/vww_uart.elf / .bin / .hex
```

If the toolchain is not on `PATH`:

```bash
ARM_GCC=/path/to/gcc-arm-none-eabi/bin bash build_h743_vww_uart.sh
```

> If you do not want to build, use `prebuilt/vww_uart.elf` directly.

---

## 5. Flashing

```bash
pyocd erase -t stm32h743xx --chip
pyocd flash -t stm32h743xx out/vww_uart.elf
```

You can also use STM32CubeProgrammer or OpenOCD to flash `.hex` or `.bin`
(the `.bin` load address is `0x08000000`).

Boot message (115200 8N1):
```
=== H743 VWW person detection (UART RGB565) ===
arena used = 82308
READY
```

---

## 6. Operation (send an image for inference)

### Step 1: put images in a folder

Supported formats: `.jpg / .jpeg / .png / .bmp`, e.g. `C:\Users\Lance\Downloads\VWW_PIC`.
Use photos with a **half- or full-body subject and normal lighting** (tight face crops are more
likely to fail; see §8).

### Step 2: convert (WSL)

```powershell
wsl -d Hermes /root/tinyml-venv/bin/python3 "<repo>/pc/prep_images.py" /mnt/c/Users/Lance/Downloads/VWW_PIC /root/tinyml-lab/vww_uart
```

Copy the outputs to `raw\` on Windows:

```powershell
wsl -d Hermes bash -c "mkdir -p /mnt/c/Users/Lance/Downloads/VWW_PIC/raw && cp /root/tinyml-lab/vww_uart/*.rgb565 /root/tinyml-lab/vww_uart/*.i8 /mnt/c/Users/Lance/Downloads/VWW_PIC/raw/"
```

### Step 3: send (Windows)

Make sure the serial port is not in use by another program (the examples use `COM9`):

```powershell
python "<repo>\pc\send_images.py" COM9 C:\Users\Lance\Downloads\VWW_PIC\raw
```

Expected output (about 3–4 s per image):

```
person_1     RESULT person 0.953 0.047
noperson_1   RESULT not_person 0.324 0.676
```

- `RESULT <class> <p_person> <p_not>`; watch the D6 LED (green/blue/red) as well.
- The board does **not** store images or results; everything is reported over UART.

### Optional: verify PC/board consistency

```powershell
wsl -d Hermes /root/tinyml-venv/bin/python3 "<repo>/pc/check_parity.py" /mnt/c/Users/Lance/Downloads/VWW_PIC/raw
```

Expect `identical=N diff=0 missing=0` (9,216 B byte-identical per image).

---

## 7. UART Protocol

| Direction | Payload |
|-----------|---------|
| PC → board | `0xA5 0x5A` + `w` (u16 LE) + `h` (u16 LE) + `w*h*2` bytes (RGB565 LE) |
| board → PC | `RESULT <person|not_person> <p_person> <p_not>` |
| Diagnostics | `P` → `PONG`; `D` → dump the last 96×96 int8 tensor (9,216 B) |

- The board receives frames by **polling** (no interrupt): after the header it reads exactly
  `w*h*2` bytes, then runs inference.
- A gap of more than 3 s between bytes aborts the frame (`ERR frame <got>/<total>`).

---

## 8. Results and Limitations

| Item | Result |
|------|--------|
| 10 test images (5 people + 5 scenes) | **9/10 correct** |
| PC vs board tensors | **10/10 byte-identical** |
| Throughput | 38,400 B ≈ 3.3 s/image (115200) |
| Failure case | tight face crop classified as not_person (0.516); the model is trained mostly on half/full-body subjects |

Other limitations:
- The camera path (OV2640/DCMI/XCLK/DMA ping-pong) is not implemented yet.
- Do **not** use pyOCD to read memory while streaming frames (it halts the core and causes
  UART overruns).

---

## 9. License and Third-Party Components

- Project code: **Apache License 2.0** (see `LICENSE`).
- Third-party components (TFLM, CMSIS, CMSIS-NN, gemmlowp, kissfft, FlatBuffers, ST HAL,
  ST device headers/startup/system, and the bundled TFLM `person_detect` model): see **`NOTICE`**
  for sources and licenses.

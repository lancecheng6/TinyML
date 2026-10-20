/* ============================================================================
 * stm32h743.ld — STM32H743XIH6 (Wildfire H7Pro core board) GCC linker script
 *
 * Translated from /root/FreeRTOS/Lesson14c_TopPanel/Output/Fire_FreeRTOS.sct.
 * Original scatter (Keil, verified):
 *   LR_IROM1 0x08000000 0x00200000 { ER_IROM1 {RESET +First; .ANY(+RO +XO)} }
 *   RW_IRAM1 0x20000000 0x00020000 { .ANY(+RW +ZI) }   ; DTCM 128 KB
 *   RW_IRAM2 0x24000000 0x00080000 { .ANY(+RW +ZI) }   ; AXI SRAM 512 KB
 *
 * Memory placement strategy:
 *   FLASH (2 MB)  : code + vector table + read-only data (incl. .tflite model const array)
 *   DTCM  (128 KB): stack / heap / .data / .bss / ★Tensor Arena (fastest, not affected by cache)
 *   AXI   (512 KB): ★DMA buffers (SAI audio / DCMI image) and other RW
 *
 * Usage (example):
 *   arm-none-eabi-gcc ... -T stm32h743.ld -Wl,-Map=app.map -o app.elf
 *   arm-none-eabi-size app.elf
 *
 * Note: DTCM is only attached to the CPU D-bus; DMA/MDMA cannot access it,
 *       so any DMA buffer must live in AXI (.sram section).
 * ==========================================================================*/

ENTRY(Reset_Handler)

/* Highest address of the user mode stack (stack grows down from the top of DTCM) */
_estack = ORIGIN(DTCM) + LENGTH(DTCM);

_Min_Heap_Size  = 0x800;    /* 2 KB heap */
_Min_Stack_Size = 0x1000;   /* 4 KB stack (adjust as needed) */

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K   /* 2 MB internal flash */
  DTCM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K    /* fastest; preferred for arena */
  AXI   (rwx) : ORIGIN = 0x24000000, LENGTH = 512K    /* DMA-reachable; image/audio buffers */
  ITCM  (rx)  : ORIGIN = 0x00000000, LENGTH = 64K     /* optional: zero-wait instruction TCM */
}

SECTIONS
{
  /* -- Vector table (must be at the very start of flash; scatter's RESET +First) -- */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  /* -- Code + read-only data (incl. .tflite model) -- */
  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  /* ARM exception index (.ARM.exidx) */
  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  /* .data initial values live in flash; startup copies them to DTCM at boot */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > DTCM AT> FLASH

  /* -- Zero-initialized data (incl. the default Tensor Arena if not placed in a named section) -- */
  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > DTCM

  /* -- ★Custom section: Tensor Arena (in DTCM: fastest and immune to D-Cache) --
   *   Usage: uint8_t tensor_arena[ARENA] __attribute__((section(".dtcm"))); */
  .dtcm (NOLOAD) :
  {
    . = ALIGN(16);
    *(.dtcm)
    *(.dtcm*)
    . = ALIGN(16);
  } > DTCM

  /* -- ★Custom section: DMA buffers (in AXI SRAM, reachable by DMA) --
   *   Usage: uint16_t pic[2*160*120] __attribute__((section(".sram")));
   *   The STM32H750 blueprint (common_use.c) uses this for DCMI ping-pong buffers. */
  .sram (NOLOAD) :
  {
    . = ALIGN(32);            /* cache-line aligned, eases cache maintenance */
    __sram_start = .;
    *(.sram)
    *(.sram*)
    . = ALIGN(32);
    __sram_end = .;
  } > AXI

  /* -- Check that heap/stack fit -- */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } > DTCM

  .ARM.attributes 0 : { *(.ARM.attributes) }
}

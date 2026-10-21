"""send_images.py — send RGB565 frames to the H743 VWW firmware over UART.

Run on Windows (needs pyserial):
    python send_images.py COM9 C:\\path\\to\\raw_dir

Protocol (see src/person_uart_app.cc):
    [0xA5][0x5A][w_lo][w_hi][h_lo][h_hi][w*h*2 bytes RGB565 LE]
Board replies: RESULT <person|not_person> <p_person> <p_not>
Then 'D' dumps the 96x96 int8 tensor (for parity checks).
"""
import glob
import os
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM9"
DIR = sys.argv[2] if len(sys.argv) > 2 else r"C:\Users\Lance\Downloads\VWW_PIC\raw"
W, H = 160, 120
TENSOR = 96 * 96

s = serial.Serial(PORT, 115200, timeout=0.5)
s.reset_input_buffer()

files = sorted(glob.glob(os.path.join(DIR, "*.rgb565")))
print("files:", len(files))
for path in files:
    name = os.path.splitext(os.path.basename(path))[0]
    data = open(path, "rb").read()
    if len(data) != W * H * 2:
        print(name, "BAD SIZE", len(data))
        continue
    # Drop stale bytes so the frame starts exactly at the magic.
    s.reset_input_buffer()
    s.write(bytes([0xA5, 0x5A, W & 0xFF, W >> 8, H & 0xFF, H >> 8]) + data)
    s.flush()

    line = b""
    t0 = time.time()
    while time.time() - t0 < 15.0:
        c = s.read(256)
        if c:
            line += c
            if b"RESULT" in line or b"ERR" in line:
                break
    print("%-12s %s" % (name, line.decode("ascii", "replace").strip().replace("\r\n", " | ")))

    s.write(b"D")
    s.flush()
    buf = b""
    t0 = time.time()
    while time.time() - t0 < 10.0:
        c = s.read(4096)
        if c:
            buf += c
            if b"TENSOR_END" in buf:
                break
    if b"TENSOR_START" in buf:
        body = buf.split(b"TENSOR_START", 1)[1].lstrip(b"\r\n").split(b"\r\nTENSOR_END", 1)[0]
        if len(body) >= TENSOR:
            open(os.path.join(DIR, name + ".board.i8"), "wb").write(body[:TENSOR])
            print("             tensor dumped")
        else:
            print("             tensor SHORT", len(body))
    else:
        print("             no tensor dump")

    # Give the board time to return to the read loop before the next frame.
    time.sleep(0.3)

s.close()

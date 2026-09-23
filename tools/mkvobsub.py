#!/usr/bin/env python3
"""Write a VobSub pair (.idx + .sub) to test DVD picture subtitles with.

usage: mkvobsub.py OUT_STEM
  Makes OUT_STEM.idx and OUT_STEM.sub: three subtitles, one every two
  seconds, each a solid bar near the bottom of a 720x480 frame in a
  different palette colour, two seconds long.
"""
import struct
import sys


def rle_line(width, colour):
    """One line: runs of at most 255 pixels, then aligned to a byte."""
    nibbles = []
    left = width
    while left > 0:
        run = min(left, 255)
        v = (run << 2) | colour
        # the shortest encoding that holds this value
        if v < 0x10:
            nibbles += [v]
        elif v < 0x40:
            nibbles += [v >> 4, v & 0xF]
        elif v < 0x100:
            nibbles += [0, v >> 4, v & 0xF]
        else:
            nibbles += [0, v >> 8, (v >> 4) & 0xF, v & 0xF]
        left -= run
    if len(nibbles) % 2:
        nibbles.append(0)
    return bytes((nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2))


def subpicture(width, height, x, y, colour, seconds):
    top = b"".join(rle_line(width, colour) for _ in range(0, height, 2))
    bottom = b"".join(rle_line(width, colour) for _ in range(1, height, 2))
    data = bytearray(4)
    top_at = len(data)
    data += top
    bottom_at = len(data)
    data += bottom
    control = len(data)
    first = bytearray()
    first += b"\x00\x00"                      # show at once
    first += b"\x00\x00"                      # filled in below: where the next sequence is
    first += b"\x01"
    first += b"\x03\x01\x23"                  # colours 3,2,1,0 <- palette 0,1,2,3
    first += b"\x04\xff\xf0"                  # colour 0 clear, the rest solid
    x2, y2 = x + width - 1, y + height - 1
    first += bytes([0x05, x >> 4, ((x & 0xF) << 4) | (x2 >> 8), x2 & 0xFF,
                    y >> 4, ((y & 0xF) << 4) | (y2 >> 8), y2 & 0xFF])
    first += bytes([0x06]) + struct.pack(">HH", top_at, bottom_at)
    first += b"\xff"
    second_at = control + len(first)
    first[2:4] = struct.pack(">H", second_at)
    second = struct.pack(">HH", int(seconds * 90000 / 1024), second_at) + b"\x02\xff"
    data += first + second
    data[0:2] = struct.pack(">H", len(data))
    data[2:4] = struct.pack(">H", control)
    return bytes(data)


def pack(spu, pts_seconds):
    """A pack header and as many private-stream-1 packets as it takes."""
    out = bytearray()
    out += b"\x00\x00\x01\xba" + b"\x44" + bytes(8) + b"\xf8"
    first = True
    while spu:
        room = 2020
        piece, spu = spu[:room], spu[room:]
        header = bytearray(b"\x81")
        if first:
            ticks = int(pts_seconds * 90000)
            header += b"\x80\x05" + bytes([
                0x20 | ((ticks >> 29) & 0x0E) | 1,
                (ticks >> 22) & 0xFF,
                ((ticks >> 14) & 0xFE) | 1,
                (ticks >> 7) & 0xFF,
                ((ticks << 1) & 0xFE) | 1,
            ])
        else:
            header += b"\x00\x00"
        body = bytes(header) + b"\x20" + piece
        out += b"\x00\x00\x01\xbd" + struct.pack(">H", len(body)) + body
        first = False
    return bytes(out)


def stamp(seconds):
    ms = int(round(seconds * 1000))
    return "%02d:%02d:%02d:%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def main():
    stem = sys.argv[1]
    palette = ["000000", "ffffff", "ff4040", "40ff40", "4040ff"] + ["%06x" % (i * 0x111111) for i in range(11)]
    sub = bytearray()
    lines = [
        "# made by mkvobsub.py",
        "size: 720x480",
        "palette: " + ", ".join(palette),
        "langidx: 0",
        "",
        "id: en, index: 0",
    ]
    for i, (when, colour) in enumerate([(1.0, 1), (3.0, 2), (5.0, 3)]):
        filepos = len(sub)
        sub += pack(subpicture(400, 60, 160, 380, colour, 2.0), when)
        lines.append("timestamp: %s, filepos: %09x" % (stamp(when), filepos))
    open(stem + ".idx", "w", newline="\n").write("\n".join(lines) + "\n")
    open(stem + ".sub", "wb").write(bytes(sub))
    print("%s.idx and %s.sub: %d bytes" % (stem, stem, len(sub)))


if __name__ == "__main__":
    main()

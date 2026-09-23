#!/usr/bin/env python3
"""Write a Blu-ray subtitle file (.sup) to test PGS picture subtitles with.

usage: mkpgs.py OUT.sup
  Makes OUT.sup: three subtitles, one every three seconds, each a solid
  bar near the bottom of a 1920x1080 frame in a different colour, two
  seconds long. Name it after the film — `movie.sup` beside `movie.mp4` —
  and the viewer offers it as a subtitle track.
"""
import sys

FRAME_W, FRAME_H = 1920, 1080
BAR_W, BAR_H = 600, 80
BAR_X, BAR_Y = (FRAME_W - BAR_W) // 2, 900

# Y'CbCr, the way a disc carries them: white, yellow, and pale blue.
COLOURS = [(235, 128, 128), (210, 16, 146), (188, 202, 112)]


def segment(type_, seconds, body):
    pts = int(seconds * 90000)
    return b"PG" + pts.to_bytes(4, "big") + b"\0\0\0\0" + bytes([type_]) + \
        len(body).to_bytes(2, "big") + body


def pcs(seconds, draws):
    body = bytearray(11)
    body[0:2] = FRAME_W.to_bytes(2, "big")
    body[2:4] = FRAME_H.to_bytes(2, "big")
    body[4] = 0x10                      # frame rate
    body[7] = 0x80                      # the start of an epoch
    body[10] = 1 if draws else 0
    if draws:
        body += (0).to_bytes(2, "big")  # object 0
        body += bytes([0, 0])           # window 0, not forced, not cropped
        body += BAR_X.to_bytes(2, "big")
        body += BAR_Y.to_bytes(2, "big")
    return segment(0x16, seconds, bytes(body))


def wds(seconds):
    body = bytes([1, 0]) + BAR_X.to_bytes(2, "big") + BAR_Y.to_bytes(2, "big") + \
        BAR_W.to_bytes(2, "big") + BAR_H.to_bytes(2, "big")
    return segment(0x17, seconds, body)


def pds(seconds, colour):
    y, cb, cr = colour
    body = bytes([0, 0]) + bytes([1, y, cr, cb, 255]) + bytes([0, 16, 128, 128, 0])
    return segment(0x14, seconds, body)


def rle_line(width, index):
    """One line of one colour, then the end of the line."""
    out = bytearray()
    left = width
    while left > 0:
        run = min(left, 16383)
        out += bytes([0x00, 0xC0 | (run >> 8), run & 0xFF, index])
        left -= run
    out += bytes([0x00, 0x00])
    return bytes(out)


def ods(seconds):
    picture = rle_line(BAR_W, 1) * BAR_H
    body = bytes([0, 0, 0, 0xC0])                      # object 0, first and last
    body += (len(picture) + 4).to_bytes(3, "big")
    body += BAR_W.to_bytes(2, "big") + BAR_H.to_bytes(2, "big")
    return segment(0x15, seconds, body + picture)


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip())
        return 2
    out = bytearray()
    for i, colour in enumerate(COLOURS):
        at = 1.0 + 3.0 * i
        out += pcs(at, True) + wds(at) + pds(at, colour) + ods(at) + segment(0x80, at, b"")
        out += pcs(at + 2.0, False) + segment(0x80, at + 2.0, b"")
    with open(sys.argv[1], "wb") as f:
        f.write(out)
    print(f"{sys.argv[1]}: {len(COLOURS)} subtitles, {len(out)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())

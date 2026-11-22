"""Writes the tile and splash pictures the Xbox/Windows package needs (Assets/*.png), with only the standard library.

An original, plain design: the game's deep-blue sky over a grey road with a yellow centre line and a yellow car.
Run it again after changing the design: python3 platform/uwp/make_logos.py
"""
import os
import struct
import zlib

SKY, ROAD, LINE, CAR, ROOF = (29, 60, 138), (70, 74, 82), (250, 214, 60), (246, 196, 40), (250, 250, 250)
SIZES = {"Square44x44Logo": (44, 44), "Square150x150Logo": (150, 150), "StoreLogo": (50, 50),
         "Wide310x150Logo": (310, 150), "SplashScreen": (620, 300)}


def pixel(x, y, w, h):
    s = min(w, h)
    cx, cy = w / 2, h * 0.62
    road_top = h * 0.55
    car_w, car_h = s * 0.42, s * 0.16
    if abs(x - cx) < car_w / 2 and cy - car_h * 1.6 < y < cy + car_h * 0.4:
        if y >= cy - car_h * 0.6:
            return CAR
        if abs(x - cx) < car_w * 0.3:
            return ROOF
    if y < road_top:
        return SKY
    if abs(y - h * 0.8) < s * 0.025 and int(x / (s * 0.12)) % 2 == 0:
        return LINE
    return ROAD


def png(w, h):
    rows = b"".join(b"\x00" + bytes(c for x in range(w) for c in pixel(x, y, w, h)) for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Assets")
    os.makedirs(out, exist_ok=True)
    for name, (w, h) in SIZES.items():
        with open(os.path.join(out, name + ".png"), "wb") as f:
            f.write(png(w, h))
        print(name, w, h)

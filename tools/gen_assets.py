#!/usr/bin/env python3
"""生成多合一挂坠的默认占位素材包(P1 规格允许 AI/占位素材)。

输出(写入 main/assets_default/):
  pendant_default_bundle.bin  —— APB1 皮肤包(与服务端 tools 打包格式逐字节一致)
  pendant_victory.pcm         —— 胜利音效 16kHz/mono/s16le
  pendant_beep.pcm            —— 堆满提示音

APB1 格式:
  magic "APB1" | u16 version | u16 file_count |
  每文件: u16 name_len + name + u32 data_len + data(全小端)

帧数据格式(与固件/服务三端一致的契约):
  - map.bin: 不透明 RGB565,小端 u16 逐帧拼接(240x160 单帧)。
  - 其余精灵(run/fight/sleep/victory/deco/rain/snow): RGB565A8,
    每帧 = 颜色平面(w*h*2, 小端) + A8 透明平面(w*h),逐帧拼接。
只用标准库,绘制为程序化像素画,确定性输出(固定随机种子)。
"""
import json
import math
import random
import struct
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "main" / "assets_default"

# ---------------------------------------------------------------- 颜色/画布
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


C = {
    "ink": rgb565(23, 48, 74),
    "body": rgb565(126, 232, 178),
    "body_hi": rgb565(216, 255, 240),
    "cheek": rgb565(255, 179, 193),
    "white": rgb565(255, 255, 255),
    "sky_a": rgb565(191, 232, 255),
    "sky_b": rgb565(232, 247, 255),
    "cloud": rgb565(250, 253, 255),
    "hill": rgb565(94, 197, 148),
    "hill_hi": rgb565(126, 220, 170),
    "grass": rgb565(70, 170, 120),
    "soil": rgb565(176, 136, 90),
    "soil_dk": rgb565(150, 112, 72),
    "path": rgb565(222, 196, 158),
    "stone": rgb565(180, 172, 160),
    "bush": rgb565(60, 160, 110),
    "bush_hi": rgb565(90, 200, 140),
    "rain": rgb565(120, 170, 255),
    "snow": rgb565(245, 250, 255),
    "enemy": rgb565(255, 105, 97),
    "steel": rgb565(225, 232, 240),
    "gold": rgb565(255, 209, 84),
}


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        # None = 未绘制(输出为全透明);精灵用 RGB565A8,背景不再霸占黑色。
        self.px = [[None] * w for _ in range(h)]

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = c

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, c)

    def disc(self, cx, cy, r, c):
        for yy in range(cy - r, cy + r + 1):
            for xx in range(cx - r, cx + r + 1):
                if (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r:
                    self.set(xx, yy, c)

    def line(self, x0, y0, x1, y1, c):
        n = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(n + 1):
            self.set(round(x0 + (x1 - x0) * i / n),
                     round(y0 + (y1 - y0) * i / n), c)

    def pack(self):
        """不透明 RGB565(地图条带用)。未绘制像素落黑色。"""
        b = bytearray()
        for row in self.px:
            for c in row:
                b += struct.pack("<H", c if c is not None else 0)
        return bytes(b)

    def pack_alpha(self):
        """RGB565A8:颜色平面(w*h*2) + A8 透明平面(w*h),精灵用。"""
        color = bytearray()
        alpha = bytearray()
        for row in self.px:
            for c in row:
                color += struct.pack("<H", c if c is not None else 0)
                alpha.append(0 if c is None else 255)
        return bytes(color) + bytes(alpha)


# ---------------------------------------------------------------- 宠物帧
def draw_pet(cv, frame, total, mode):
    """共用的团子身体;mode: run/fight/sleep/victory"""
    bob = {"run": int(round(1.5 * math.sin(2 * math.pi * frame / total))),
           "fight": int(round(1 * math.sin(2 * math.pi * frame / total))),
           "sleep": 1, "victory": int(round(-4 * abs(math.sin(math.pi * frame / max(total - 1, 1)))))}.get(mode, 0)
    # 整体放大 1.25x:身体 r16→r20,躯干中心下移贴近地面
    cx, cy = 32, 38 + bob
    # 腿(睡觉不画跑步腿)
    if mode == "run":
        for k, lx in enumerate((22, 38)):
            sw = 3 * math.sin(2 * math.pi * frame / total + k * math.pi)
            cv.rect(lx - 3, 54 + bob, 7, 6 + int(abs(sw)), C["body"])
            cv.rect(lx - 3, 54 + bob, 7, 2, C["ink"] >> 1 | 0x2104)
    else:
        cv.rect(20, 54 + bob, 8, 5, C["body"])
        cv.rect(36, 54 + bob, 8, 5, C["body"])
    # 身体与肚皮(r20,贴地)
    cv.disc(cx, cy, 20, C["body"])
    cv.disc(cx, cy + 6, 12, C["body_hi"])
    # 呆毛
    cv.line(cx, cy - 20, cx + 3, cy - 25, C["ink"])
    cv.set(cx + 3, cy - 26, C["cheek"])
    # 眼睛/腮红
    if mode == "sleep":
        cv.rect(cx - 10, cy - 4, 6, 2, C["ink"])
        cv.rect(cx + 4, cy - 4, 6, 2, C["ink"])
        if frame % 2 == 0:
            cv.line(48, 14 + bob, 53, 9 + bob, C["ink"])
            cv.line(53, 9 + bob, 56, 9 + bob, C["ink"])
            cv.line(56, 14 + bob, 60, 9 + bob, C["ink"])
    else:
        cv.disc(cx - 8, cy - 5, 3, C["ink"])
        cv.disc(cx + 6, cy - 5, 3, C["ink"])
        cv.set(cx - 9, cy - 6, C["white"])
        cv.set(cx + 5, cy - 6, C["white"])
        cv.rect(cx - 14, cy + 2, 4, 3, C["cheek"])
        cv.rect(cx + 11, cy + 2, 4, 3, C["cheek"])
    # 嘴
    cv.line(cx - 3, cy + 6, cx + 3, cy + 6, C["ink"])

    if mode == "fight":
        ang = math.pi * (0.25 + 0.35 * math.sin(2 * math.pi * frame / total))
        sx, sy = cx + 12, cy - 8
        ex = sx + int(18 * math.cos(ang))
        ey = sy - int(18 * math.sin(ang))
        cv.line(sx, sy, ex, ey, C["steel"])
        cv.line(sx - 1, sy + 1, sx + 4, sy - 4, C["ink"])
        if frame in (3, 4):   # 挥砍弧光
            for t in range(-10, 11):
                cv.set(cx + 17 + t, cy - 10 + abs(t) // 2, C["white"])
        eb = int(2 * math.sin(2 * math.pi * frame / total))
        cv.disc(55, 42 + eb, 7, C["enemy"])
        cv.set(52, 40 + eb, C["ink"])
        cv.set(58, 40 + eb, C["ink"])
    if mode == "victory":
        cv.line(cx - 14, cy - 3, cx - 22, cy - 15, C["body"])
        cv.line(cx + 14, cy - 3, cx + 22, cy - 15, C["body"])
        cv.disc(cx - 22, cy - 16, 3, C["cheek"])
        cv.disc(cx + 22, cy - 16, 3, C["cheek"])
        for t in range(5):
            a = 2 * math.pi * (frame + t) / total
            cv.set(10 + int(10 * math.cos(a)), 12 + int(8 * math.sin(a)), C["gold"])


def frames_pet(mode, count):
    out = bytearray()
    for f in range(count):
        cv = Canvas(64, 64)
        draw_pet(cv, f, count, mode)
        out += cv.pack_alpha()      # 精灵带透明
    return bytes(out)


# ---------------------------------------------------------------- 地图条带
def map_strip():
    cv = Canvas(240, 160)
    for y in range(0, 100):   # 天空渐变(周期无关)
        t = y / 99
        r = int(191 + (232 - 191) * t)
        g = int(232 + (247 - 232) * t)
        b = int(255 + (255 - 255) * t)
        cv.rect(0, y, 240, 1, rgb565(r, g, b))
    # 云:x 周期 120 与 80,均可整除 240,首尾无缝
    for x0 in range(0, 240, 120):
        cv.disc(x0 + 30, 24, 7, C["cloud"])
        cv.disc(x0 + 40, 20, 9, C["cloud"])
        cv.disc(x0 + 50, 25, 6, C["cloud"])
    for x0 in range(0, 240, 80):
        cv.disc(x0 + 20, 46, 5, C["cloud"])
        cv.disc(x0 + 28, 43, 6, C["cloud"])
    # 远山:正弦周期 240
    for x in range(240):
        hy = 104 + int(9 * math.sin(2 * math.pi * x / 240))
        for y in range(hy, 130):
            cv.set(x, y, C["hill_hi"] if y < hy + 3 else C["hill"])
    # 草地边缘
    for x in range(240):
        gy = 126 + int(2 * math.sin(4 * math.pi * x / 240))
        for y in range(gy, 132):
            cv.set(x, y, C["grass"])
    # 土地 + 小径
    cv.rect(0, 130, 240, 30, C["soil"])
    cv.rect(0, 136, 240, 16, C["path"])
    rng = random.Random(7)   # 固定种子,确定性输出;逐 x 取样保证首尾循环
    for x in range(240):
        if x % 24 == 5:
            cv.rect(x, 142, 4, 2, C["stone"])
        for _ in range(2):
            y = 131 + (x * 7 + 3) % 28
            if 136 <= y < 152:
                continue
            cv.set(x, y, C["soil_dk"] if (x * 13 + y) % 5 else C["soil"])
    return cv.pack()


def deco_bush():
    cv = Canvas(24, 24)   # 装扮同样透明
    cv.disc(9, 16, 7, C["bush"])
    cv.disc(16, 15, 6, C["bush"])
    cv.disc(10, 12, 4, C["bush_hi"])
    cv.rect(11, 21, 3, 3, C["ink"])
    return cv.pack_alpha()


def weather_frames(kind):
    out = bytearray()
    for f in range(2):
        cv = Canvas(16, 16)
        if kind == "rain":
            for k in range(3):
                x = 2 + k * 5
                y = (k * 6 + f * 5) % 14
                cv.line(x, y, x + 1, y + 3, C["rain"])
        else:
            pts = [(4, 4), (11, 5), (6, 11), (12, 12), (8, 8)]
            for i, (x, y) in enumerate(pts):
                dy = (i + f) % 2
                cv.disc(x, y + dy, 1, C["snow"])
        out += cv.pack_alpha()   # 精灵契约:RGB565A8(此前漏改导致设备拒载)
    return bytes(out)


# ---------------------------------------------------------------- 音效
def synth_victory():
    sr = 16000
    # 短版 ~0.85s:快速上行琶音+一个延长高音(规格:不要太长,任意键可断)
    notes = [(523.25, .0, .14), (659.25, .14, .14), (783.99, .28, .14),
             (1046.5, .42, .40)]
    total = int(sr * 0.85)
    buf = [0.0] * total
    for freq, t0, dur in notes:
        n0, n1 = int(t0 * sr), min(int((t0 + dur) * sr), total)
        for i in range(n0, n1):
            t = (i - n0) / sr
            env = min(1.0, t * 60) * math.exp(-3.2 * t)
            sq = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
            tri = 2 / math.pi * math.asin(math.sin(2 * math.pi * freq * t))
            buf[i] += 0.30 * env * (0.6 * sq + 0.4 * tri)
    return b"".join(struct.pack("<h", max(-32767, min(32767, int(v * 32767)))) for v in buf)


def synth_beep():
    sr = 16000
    out = bytearray()
    for i in range(int(sr * 0.09)):
        t = i / sr
        env = min(1.0, t * 200) * math.exp(-28 * t)
        v = math.sin(2 * math.pi * 1320 * t) * 0.5 * env
        out += struct.pack("<h", int(v * 32767))
    return bytes(out)


# ---------------------------------------------------------------- APB1 打包
def pack_bundle(version, files):
    body = bytearray()
    body += b"APB1"
    body += struct.pack("<HH", version, len(files))
    for name, data in files:
        nb = name.encode("utf-8")
        body += struct.pack("<H", len(nb)) + nb
        body += struct.pack("<I", len(data)) + data
    return bytes(body)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = {
        "version": 1,
        "actions": {
            "run": {"file": "run.bin", "frames": 6, "fps": 6, "w": 64, "h": 64},
            "fight": {"file": "fight.bin", "frames": 6, "fps": 8, "w": 64, "h": 64},
            "sleep": {"file": "sleep.bin", "frames": 2, "fps": 2, "w": 64, "h": 64},
            "victory": {"file": "victory.bin", "frames": 4, "fps": 6, "w": 64, "h": 64},
        },
        "map": {"file": "map.bin", "w": 240, "h": 160},
        "decorations": [{"file": "deco0.bin", "w": 24, "h": 24}],
        "weather": {
            "rain": {"file": "rain.bin", "frames": 2, "fps": 6, "w": 16, "h": 16},
            "snow": {"file": "snow.bin", "frames": 2, "fps": 4, "w": 16, "h": 16},
        },
    }
    files = [
        ("manifest.json", json.dumps(manifest, ensure_ascii=False, indent=1).encode("utf-8")),
        ("run.bin", frames_pet("run", 6)),
        ("fight.bin", frames_pet("fight", 6)),
        ("sleep.bin", frames_pet("sleep", 2)),
        ("victory.bin", frames_pet("victory", 4)),
        ("map.bin", map_strip()),
        ("deco0.bin", deco_bush()),
        ("rain.bin", weather_frames("rain")),
        ("snow.bin", weather_frames("snow")),
    ]
    bundle = pack_bundle(1, files)
    (OUT / "pendant_default_bundle.bin").write_bytes(bundle)
    (OUT / "pendant_victory.pcm").write_bytes(synth_victory())
    (OUT / "pendant_beep.pcm").write_bytes(synth_beep())
    total = len(bundle)
    print(f"bundle: {total} bytes ({total/1024:.1f} KiB)")
    for name, data in files:
        print(f"  {name:16s} {len(data):>7d}")
    print(f"victory.pcm {len(synth_victory())}B, beep.pcm {len(synth_beep())}B")


if __name__ == "__main__":
    sys.exit(main())

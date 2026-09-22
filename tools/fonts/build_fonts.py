#!/usr/bin/env python3
"""构建应用中文字体子集(LVGL)并校验覆盖。

流程:
  1. 扫描 main/**/*.c|h 中的全部 CJK/全角字符(含 app_fmt 的动态串)
  2. 与基础字符集(ASCII + 常用标点 + 数字单位)求并集
  3. 用 fonttools 检查字体 cmap 覆盖,缺字形直接报错(防真机空白方框)
  4. 调 lv_font_conv(npm, 固定版本)生成 12px/24px 两档 LVGL 字体源码

用法: python3 tools/fonts/build_fonts.py
前置: pip3 install fonttools;网络可用(npx 拉取 lv_font_conv)。
字体: assets/fonts/fusion-pixel-12px-proportional-{zh_hans,latin}.ttf (OFL)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
FONT_ZH = ROOT / "assets" / "fonts" / "fusion-pixel-12px-proportional-zh_hans.ttf"
FONT_LA = ROOT / "assets" / "fonts" / "fusion-pixel-12px-proportional-latin.ttf"
OUT_DIR = ROOT / "main" / "fonts"
LV_FONT_CONV_VERSION = "1.5.3"

# 基础字符:可打印 ASCII + 常用中英文标点 + 度/点等符号
BASE = (
    "".join(chr(c) for c in range(0x20, 0x7F))
    + "·—…～°％：；！？、。，“”‘’（）《》【】"
)

# CJK 与全角区段(扫描目标)
CJK_RANGES = (
    (0x2E80, 0x9FFF),    # CJK 部首/汉字
    (0x3000, 0x303F),    # CJK 标点
    (0xFF00, 0xFFEF),    # 全角形式
)


def scan_sources():
    chars = set()
    for path in (ROOT / "main").rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        text = path.read_text(encoding="utf-8")
        for ch in text:
            cp = ord(ch)
            if any(lo <= cp <= hi for lo, hi in CJK_RANGES):
                chars.add(ch)
    return chars


def check_coverage(glyphs):
    from fontTools.ttLib import TTFont
    fonts = [TTFont(str(p), fontNumber=0) for p in (FONT_ZH, FONT_LA)]
    cmaps = [f.getBestCmap() for f in fonts]
    missing = [ch for ch in sorted(glyphs)
               if not any(ord(ch) in cmap for cmap in cmaps)]
    if missing:
        print("缺失字形的字符:", " ".join(missing), file=sys.stderr)
        sys.exit(1)


def build_size(size, glyphs):
    out = OUT_DIR / f"app_font_{size}.c"
    cmd = [
        "npx", "--yes", f"lv_font_conv@{LV_FONT_CONV_VERSION}",
        "--font", str(FONT_ZH), "--symbols", "".join(sorted(glyphs)),
        "--font", str(FONT_LA), "-r", "0x20-0x7E",
        "--size", str(size), "--bpp", "4", "--format", "lvgl",
        "--no-compress", "--no-prefilter",
        "--lv-font-name", f"app_font_{size}", "--lv-include", "lvgl.h",
        "--output", str(out),
    ]
    subprocess.run(cmd, check=True)
    kb = out.stat().st_size / 1024
    print(f"app_font_{size}.c  {kb:.1f} KiB  ({len(glyphs)} 字符)")


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    glyphs = set(BASE) | scan_sources()
    print(f"共 {len(glyphs)} 个字符(基础 {len(set(BASE))} + 源码扫描 "
          f"{len(glyphs - set(BASE))})")
    check_coverage(glyphs)
    for size in (12, 24):
        build_size(size, glyphs)
    print("字体生成完成 -> main/fonts/")


if __name__ == "__main__":
    main()

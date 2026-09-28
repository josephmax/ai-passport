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
import os
import json
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

# 24px 大字号用于页面/卡片标题与设置页 ADJUST 大数值 —— 单独的小字符集
# 控制 Flash 体积(全量 686 字 24px 约 1.1MB,远超规格 100–200KB 字体预算)。
TITLE_GLYPHS = "设置仪表盘宠物周额度小时本Token起始结束连接手机返回用量亮度音自动息屏作时间取消全部计经验分钟你的名字秒"
BADGE_NAME_GLYPHS = (ROOT / "main" / "fonts" / "badge_name_glyphs.txt").read_text(encoding="utf-8").strip()

# CJK 与全角区段(扫描目标)
CJK_RANGES = (
    (0x2E80, 0x9FFF),    # CJK 部首/汉字
    (0x3000, 0x303F),    # CJK 标点
    (0xFF00, 0xFFEF),    # 全角形式
)


def strip_comments(text):
    """去掉 // 与 /* */ 注释,只留代码(字符串字面量保留)。"""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def scan_sources():
    chars = set()
    for path in (ROOT / "main").rglob("*"):
        if path.suffix not in (".c", ".h"):
            continue
        text = strip_comments(path.read_text(encoding="utf-8"))
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
    # Reuse a pinned local installation when npm/network access is unavailable.
    local = os.environ.get("LV_FONT_CONV_JS")
    if local:
        converter = Path(local).resolve()
        package = json.loads((converter.parent / "package.json").read_text())
        if package["version"] != LV_FONT_CONV_VERSION:
            raise ValueError("LV_FONT_CONV_JS must point to pinned lv_font_conv 1.5.3")
        command = ["node", str(converter)]
    else:
        command = ["npx", "--yes", f"lv_font_conv@{LV_FONT_CONV_VERSION}"]
    cmd = command + [
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
    titles = set(TITLE_GLYPHS + BADGE_NAME_GLYPHS) | set(c for c in BASE if ord(c) < 0x80)
    print(f"正文 {len(glyphs)} 字符(基础 {len(set(BASE))} + 源码扫描 "
          f"{len(glyphs - set(BASE))});标题 {len(titles)} 字符")
    check_coverage(glyphs | titles)
    build_size(12, glyphs)
    build_size(24, titles)
    print("字体生成完成 -> main/fonts/")


if __name__ == "__main__":
    main()

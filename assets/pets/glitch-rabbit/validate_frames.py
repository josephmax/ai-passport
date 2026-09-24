# 动作帧像素契约校验：对 frames/ 下已保存的 PNG 逐帧检查设备端与服务端约束。
# 校验项（对应 service/src/assets/pipeline.ts 与 main/ui/ui_pet.c 的约定）：
#   1. PNG 格式、RGBA 模式、恰好 64x64
#   2. 二值透明（alpha 只能 0/255，转 RGB565A8 无半透明混色）
#   3. 脚底贴地：最底行必须有不透明像素（设备把精灵底边锚定到地面线）
#   4. 颜色全部来自 gen_sprite.P 调色板（含描边/阴影派生色），无杂色
#   5. 无奶油-薰衣草直接相邻（描边闭合，不允许断线露底）
#   6. 帧数在 ACTION_RULES 内：run/fight 4-8、sleep 2-4、victory 1-4
#   7. 帧率 1-10 fps
# 用法：python3 validate_frames.py   ->  全部通过打印 PASS，任一失败退出码 1
import sys
from pathlib import Path
import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
S = 64
ACTION_RULES = {  # 与 service/src/assets/pipeline.ts ACTION_RULES 一致
    'run': (4, 8), 'fight': (4, 8), 'sleep': (2, 4), 'victory': (1, 4),
}
ACTION_FPS = {'run': 6, 'fight': 8, 'sleep': 2, 'victory': 6}

sys.path.insert(0, str(HERE))
from gen_sprite import P  # noqa: E402  唯一调色板来源

PALETTE = [c for c in P.values()]
FRAME_BG = None  # 透明

def check_frame(path):
    errs = []
    im = Image.open(path)
    if im.format != 'PNG':
        errs.append(f'格式 {im.format} != PNG')
    im = im.convert('RGBA')
    if im.size != (S, S):
        errs.append(f'尺寸 {im.size} != (64, 64)')
        return errs
    a = np.asarray(im)
    al = a[..., 3]
    if not np.isin(al, (0, 255)).all():
        errs.append('alpha 存在半透明值（需二值）')
    opaque = al == 255
    if opaque.sum() < 800:
        errs.append(f'不透明像素过少 {int(opaque.sum())}')
    if not opaque[S - 1].any():
        errs.append('最底行无像素（脚底必须贴画布底行）')
    rgb = a[..., :3].astype(int)
    bad_px = 0
    for y, x in zip(*np.where(opaque)):
        c = rgb[y, x]
        if not any((np.abs(c - np.array(p)).max() <= 2) for p in PALETTE):
            bad_px += 1
    if bad_px:
        errs.append(f'{bad_px} 个像素不在调色板内')
    cream = (np.abs(rgb - np.array(P['cream'])).sum(-1) < 30) & opaque
    lil = opaque
    for c in (P['lil_md'], P['lil_hi'], P['lil_dp']):
        lil &= (np.abs(rgb - np.array(c)).sum(-1) < 30)
    leaks = 0
    for y, x in zip(*np.where(cream)):
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < S and 0 <= nx < S and lil[ny, nx]:
                leaks += 1
    if leaks:
        errs.append(f'{leaks} 处奶油色与脸紫直接相邻（描边断线）')
    return errs

def main():
    frames_root = HERE / 'frames'
    total_errs = 0
    for action, (lo, hi) in ACTION_RULES.items():
        d = frames_root / action
        files = sorted(d.glob('frame-*.png'))
        n = len(files)
        if not (lo <= n <= hi):
            print(f'FAIL {action}: 帧数 {n} 不在 {lo}-{hi}')
            total_errs += 1
            continue
        fps = ACTION_FPS[action]
        if not (1 <= fps <= 10):
            print(f'FAIL {action}: fps {fps} 超出 1-10')
            total_errs += 1
        act_errs = 0
        for f in files:
            errs = check_frame(f)
            if errs:
                act_errs += 1
                print(f'FAIL {f.name}: ' + '; '.join(errs))
            else:
                print(f'PASS {action}/{f.name}')
        if act_errs:
            total_errs += act_errs
        else:
            print(f'PASS {action}: {n} 帧 @ {fps}fps，全部通过')
    if total_errs:
        print(f'TOTAL FAIL: {total_errs}')
        return 1
    print('ALL FRAMES PASS')
    return 0

if __name__ == '__main__':
    sys.exit(main())

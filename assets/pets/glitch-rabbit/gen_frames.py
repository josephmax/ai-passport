# 从 gen_sprite.draw_frame 派生四个动作的全套帧，并逐帧跑设备契约自检。
# 用法：python3 gen_frames.py  ->  frames/<动作>/frame-0N.png + strip-4x.png + preview.gif
import numpy as np
from pathlib import Path
from PIL import Image
from gen_sprite import draw_frame, checks, S, P

out = Path(__file__).resolve().parent / 'frames'

ACTIONS = {
    # 跑步：身体起伏 + 耳朵前后摆 + 双脚交替（脚始终贴底行）
    'run': dict(fps=6, poses=[
        dict(bob=0, ear_sway=-2, feet='left_fwd'),
        dict(bob=1, ear_sway=-1, feet='left_fwd'),
        dict(bob=2, ear_sway=0, feet='left_fwd'),
        dict(bob=2, ear_sway=0, feet='right_fwd'),
        dict(bob=1, ear_sway=1, feet='right_fwd'),
        dict(bob=0, ear_sway=2, feet='right_fwd'),
    ]),
    # 打怪：左右拳各三拍（预备/出拳/全伸），嘴张更大
    'fight': dict(fps=8, poses=[
        dict(arms='punch', punch_side='L', punch_ext=0, mouth='open', bob=0),
        dict(arms='punch', punch_side='L', punch_ext=1, mouth='open', bob=1),
        dict(arms='punch', punch_side='L', punch_ext=2, mouth='open', bob=1),
        dict(arms='punch', punch_side='R', punch_ext=0, mouth='open', bob=0),
        dict(arms='punch', punch_side='R', punch_ext=1, mouth='open', bob=1),
        dict(arms='punch', punch_side='R', punch_ext=2, mouth='open', bob=1),
    ]),
    # 睡觉：闭眼、垂耳、小嘴微笑、呼吸下沉
    'sleep': dict(fps=2, poses=[
        dict(eyes='closed', ear_drop=6, ear_sway=-2, mouth='small', bob=0),
        dict(eyes='closed', ear_drop=7, ear_sway=-1, mouth='small', bob=1),
        dict(eyes='closed', ear_drop=7, ear_sway=-1, mouth='small', bob=1),
    ]),
    # 胜利：双臂上举、嘴张到最大、一拍下蹲蓄力
    'victory': dict(fps=6, poses=[
        dict(arms='up', mouth='wide', bob=0),
        dict(arms='up', mouth='wide', bob=-1, ear_sway=2),
        dict(arms='up', mouth='wide', bob=0),
    ]),
}

def checker(w, h, s=16):
    yy, xx = np.mgrid[0:h, 0:w]
    g = (((yy // s) + (xx // s)) % 2 * 40 + 190).astype(np.uint8)
    return np.dstack([g, g, g])

def composite(img, scale):
    big = Image.fromarray(img).resize((img.shape[1] * scale, img.shape[0] * scale), Image.NEAREST)
    al = np.asarray(big).astype(np.float32)[..., 3:] / 255
    base = checker(img.shape[1] * scale, img.shape[0] * scale)
    return Image.fromarray((np.asarray(big)[..., :3] * al + base * (1 - al)).astype(np.uint8))

for name, spec in ACTIONS.items():
    d = out / name
    d.mkdir(parents=True, exist_ok=True)
    frames = []
    for i, pose in enumerate(spec['poses'], 1):
        img = draw_frame(**pose)
        checks(img)
        f = d / f'frame-{i:02d}.png'
        Image.fromarray(img).save(f)
        frames.append(f)
        # 透明度必须为二值
        al = np.asarray(Image.open(f))[..., 3]
        assert set(np.unique(al)) <= {0, 255}
    # 条带图（4x）
    strip = Image.new('RGB', (len(frames) * S * 4, S * 4))
    for i, f in enumerate(frames):
        strip.paste(composite(np.asarray(Image.open(f)), 4), (i * S * 4, 0))
    strip.save(d / 'strip-4x.png')
    # GIF 预览（棋盘底、按动作帧率）
    dur = round(1000 / spec['fps'])
    gifs = [composite(np.asarray(Image.open(f)), 4) for f in frames]
    gifs[0].save(d / 'preview.gif', save_all=True, append_images=gifs[1:],
                 duration=dur, loop=0)
    print(f"{name}: {len(frames)} frames @ {spec['fps']}fps  bottom-row ok, closure ok")
print('done ->', out)

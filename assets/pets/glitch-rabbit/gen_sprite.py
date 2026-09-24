# 宠物关键像素资产生成脚本：64x64 参数化绘制。
# 基础姿势（默认参数）= 已定稿的 base-64.png；动作帧由 gen_frames.py 派生。
# 用法：python3 gen_sprite.py  ->  本目录生成 base-64.png / preview_4x.png / face_zoom_6x.png
import numpy as np
from pathlib import Path
from PIL import Image

S = 64
P = {
 'lil_hi':  (224, 208, 242),
 'lil_md':  (210, 192, 234),
 'lil_dp':  (183, 163, 204),
 'lil_sh':  (143, 123, 176),
 'red_md':  (209, 98, 109),
 'red_dp':  (168, 69, 79),
 'cream':   (233, 208, 135),
 'cream_sh':(196, 173, 110),
 'paw':     (233, 222, 208),
 'paw_sh':  (194, 180, 158),
 'brown':   (74, 45, 38),
}

yy_g, xx_g = np.mgrid[0:S, 0:S]

def ell(cx, cy, rx, ry):
    return ((xx_g - cx + 0.5) / rx) ** 2 + ((yy_g - cy + 0.5) / ry) ** 2 <= 1.0

def rrect(x0, y0, x1, y1):
    return (xx_g >= x0) & (xx_g < x1) & (yy_g >= y0) & (yy_g < y1)

def dilate4(m):
    d = np.zeros_like(m)
    d[1:, :] |= m[:-1, :]
    d[:-1, :] |= m[1:, :]
    d[:, 1:] |= m[:, :-1]
    d[:, :-1] |= m[:, 1:]
    return d

def limb(x0, y0, x1, y1, r):
    m = np.zeros((S, S), bool)
    for i in range(13):
        t = i / 12
        m |= ell(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, r)
    return m

def ear_shape(cx_top, cx_bot, w_top, w_bot, y_top, y_bot):
    m = ell(cx_top, y_top + 1.0, w_top * 0.95, 1.9)
    for i in range(37):
        t = i / 36
        cx = cx_bot + (cx_top - cx_bot) * t
        cy = y_bot + (y_top - y_bot) * t
        w = w_bot + (w_top - w_bot) * t
        if cy > y_top + 1.5:
            m |= ell(cx, cy, w, 1.0)
    return m

def crescent(cx, corner_y, half_w, s1, s2):
    """月牙嘴：上下圆弧相接于两尖角；返回 (描边, 牙齿) 掩码，描边闭合。"""
    r1 = half_w ** 2 / (2 * s1) + s1 / 2
    cy1 = corner_y - (r1 - s1)
    r2 = half_w ** 2 / (2 * s2) + s2 / 2
    cy2 = corner_y - (r2 - s2)
    cov = np.zeros((S, S))
    for ox in (0.125, 0.375, 0.625, 0.875):
        for oy in (0.125, 0.375, 0.625, 0.875):
            px = xx_g - 0.5 + ox; py = yy_g - 0.5 + oy
            dxp = px - cx
            yu = cy1 + np.sqrt(np.clip(r1 ** 2 - dxp ** 2, 0, None))
            yl = cy2 + np.sqrt(np.clip(r2 ** 2 - dxp ** 2, 0, None))
            cov += ((py > yu) & (py < yl) & (np.abs(dxp) <= half_w)) & (yy_g * 0 + 1)
    cov /= 16.0
    fill_m = cov >= 0.5
    ring = dilate4(fill_m) & ~fill_m
    teeth = fill_m.copy()
    sep = np.zeros_like(fill_m)
    for x in range(int(cx - half_w) + 3, int(cx + half_w), 4):
        sep |= rrect(x, 0, x + 1, S)
        teeth &= ~rrect(x, 0, x + 1, S)
    return ring, teeth, sep

# 姿势参数（默认 = 已定稿基础帧）
BASE = dict(bob=0, lean=0, ear_sway=0, ear_drop=0,
            eyes='open', mouth='grin', arms='down', feet='stand')
MOUTH_PRESETS = dict(grin=(14.5, 1.5, 6.8), open=(14.5, 1.8, 8.8),
                     small=(11.5, 1.2, 4.6), wide=(15.0, 2.0, 9.2))

def draw_frame(**over):
    p = {**BASE, **over}
    img = np.zeros((S, S, 4), np.uint8)

    def fill(m, c):
        img[m] = (*c, 255)

    head_cx = 32 + p['lean']
    head_cy = 28 - p['bob']
    body_cy = 52 - p['bob']

    # 长耳朵
    drop = p['ear_drop']
    earL = ear_shape(19.5 + p['ear_sway'], 26 + p['lean'], 4.2, 5.6, 1 + drop, 17)
    earR = ear_shape(44.5 + p['ear_sway'], 38 + p['lean'], 4.2, 5.6, 1 + drop, 17)
    ears = earL | earR
    fill(ears, P['lil_md'])
    fill(earL & (xx_g < 17.5 + p['ear_sway']) & (yy_g >= 3 + drop) & (yy_g <= 9 + drop), P['lil_hi'])
    fill(earR & (xx_g > 46.5 + p['ear_sway']) & (yy_g >= 3 + drop) & (yy_g <= 9 + drop), P['lil_hi'])
    fill(earL & (xx_g > 23 + p['ear_sway']) & (yy_g < 15 + drop), P['lil_dp'])
    fill(earR & (xx_g < 41 + p['ear_sway']) & (yy_g < 15 + drop), P['lil_dp'])

    # 头
    head = ell(head_cx, head_cy, 20, 16.5)
    fill(head, P['lil_md'])
    fill(head & ell(head_cx - 3, head_cy - 3, 15, 11), P['lil_hi'])
    fill(head & ~ell(head_cx - 2.5, head_cy - 1, 18.8, 14.8), P['lil_dp'])

    # 眼睛
    eye_cy = 27.2 - p['bob']
    if p['eyes'] == 'open':
        domeL = ell(head_cx - 9, eye_cy, 6.0, 3.3) & (yy_g <= eye_cy + 2.7) & (xx_g <= head_cx - 3)
        domeR = ell(head_cx + 9, eye_cy, 6.0, 3.3) & (yy_g <= eye_cy + 2.7) & (xx_g >= head_cx + 3)
        wedL = domeL & (xx_g >= head_cx - 7.6)
        wedR = domeR & (xx_g >= head_cx + 10.4)
        fill(domeL | domeR, P['brown'])
        fill(wedL | wedR, P['cream'])
        fill(dilate4(domeL | domeR) & ~(domeL | domeR) & head, P['brown'])
    else:  # closed：闭眼横线
        barL = ell(head_cx - 9, eye_cy + 1, 5.2, 2.0) & (yy_g >= eye_cy)
        barR = ell(head_cx + 9, eye_cy + 1, 5.2, 2.0) & (yy_g >= eye_cy)
        fill(barL | barR, P['brown'])

    # 躯干
    body = ell(head_cx, body_cy, 9.5, 10.5) & ~head
    fill(body, P['lil_md'])
    fill(body & ell(head_cx - 1.5, body_cy - 3, 6, 5.5), P['lil_hi'])


    # 手臂 + 手掌
    sh_y = 47.5 - p['bob']
    pose = p['arms']
    if pose == 'down':
        armL = limb(head_cx - 6.5, sh_y, 19.5, 54.5 - p['bob'] * 0.5, 2.3) & ~head
        armR = limb(head_cx + 6.5, sh_y, 44.5, 54.5 - p['bob'] * 0.5, 2.3) & ~head
        pawL = ell(19, 55.5 - p['bob'] * 0.5, 3.4, 3.1) & ~head
        pawR = ell(45, 55.5 - p['bob'] * 0.5, 3.4, 3.1) & ~head
    elif pose == 'punch':
        f = 1 if p['punch_side'] == 'L' else -1
        rest_cx = head_cx - f * 12.5
        full_cx = head_cx - f * 18.0
        e = p['punch_ext'] / 2
        pcx = rest_cx + (full_cx - rest_cx) * e
        pcy = (55.5 - p['bob'] * 0.5) + ((51.5 - p['bob']) - (55.5 - p['bob'] * 0.5)) * e
        armA = limb(head_cx - f * 6.5, sh_y, pcx + f * 2.0, pcy + 1.0, 2.6) & ~head
        armB = limb(head_cx + f * 6.5, sh_y, head_cx + f * 12.5, 54.5 - p['bob'] * 0.5, 2.3) & ~head
        pawA = ell(pcx, pcy, 3.8, 3.4) & ~head
        pawB = ell(head_cx + f * 12.5, 55.5 - p['bob'] * 0.5, 3.4, 3.1) & ~head
        armL, armR, pawL, pawR = (armA, armB, pawA, pawB) if p['punch_side'] == 'L' else (armB, armA, pawB, pawA)
    elif pose == 'up':
        # 举手：沿头两侧轮廓外走两段折线（肩→肘→掌），不做 ~head 裁剪
        armL = limb(head_cx - 6.5, sh_y, head_cx - 18.3, 35.5 - p['bob'], 2.4) | \
               limb(head_cx - 18.3, 35.5 - p['bob'], head_cx - 19.5, 21.5 - p['bob'], 2.3)
        armR = limb(head_cx + 6.5, sh_y, head_cx + 18.3, 35.5 - p['bob'], 2.4) | \
               limb(head_cx + 18.3, 35.5 - p['bob'], head_cx + 19.5, 21.5 - p['bob'], 2.3)
        pawL = ell(head_cx - 19.5, 19.5 - p['bob'], 3.6, 3.3)
        pawR = ell(head_cx + 19.5, 19.5 - p['bob'], 3.6, 3.3)
    fill(armL | armR, P['lil_dp'])
    fill(pawL | pawR, P['paw'])
    fill((pawL & (xx_g < 17.5)) | (pawR & (xx_g > 46.5)), P['paw_sh'])
    if pose == 'up':   # 举手时手掌压在耳朵上，需要描边分隔
        fill(dilate4(pawL | pawR) & ~(pawL | pawR), P['brown'])


    # 月牙嘴
    half_w, s1, s2 = MOUTH_PRESETS[p['mouth']]
    ring, teeth, sep = crescent(head_cx, 33.5 - p['bob'], half_w, s1, s2)
    fill(ring, P['brown'])
    # 齿缝填棕：只取与牙齿相邻、且不在描边上的缝像素
    sep_in = sep & dilate4(teeth) & ~ring
    fill(sep_in & ~teeth, P['brown'])
    fill(teeth, P['cream'])

    # 红背带裤
    ovr = np.zeros((S, S), bool)
    ovr |= rrect(24, 49 - p['bob'], 40, 60 - p['bob']) & ell(head_cx, body_cy, 9.8, 10)
    ovr |= rrect(27, 44 - p['bob'], 37, 51 - p['bob']) & ~head
    fill(ovr, P['red_md'])
    fill(ovr & (xx_g <= 26) & (yy_g <= 55 - p['bob']), P['red_dp'])
    fill(ovr & (yy_g >= 57 - p['bob']), P['red_dp'])
    for y in range(53 - p['bob'], 60 - p['bob']):
        px_ = np.zeros((S, S), bool); px_[y, 32 - (y % 2)] = True
        fill(ovr & px_, P['red_dp'])
    strap = rrect(26, 41 - p['bob'], 29, 45 - p['bob']) | rrect(35, 41 - p['bob'], 38, 45 - p['bob'])
    fill(strap & ~head, P['red_md'])
    fill(strap & ~head & (xx_g <= 27), P['red_dp'])
    fill(strap & ~head & (xx_g >= 37), P['red_dp'])
    pk = rrect(28.5, 51 - p['bob'], 35.5, 56 - p['bob']) & ovr
    fill(pk, P['red_dp'])
    fill(rrect(28.5, 51 - p['bob'], 35.5, 52 - p['bob']) & ovr, P['red_md'])
    hem = rrect(24, 58 - p['bob'], 40, 60 - p['bob']) & ell(head_cx, body_cy, 10.2, 10.5)
    fill(hem, P['red_dp'])

    # 脚（永远贴底行）
    fl = 27 + (2 if p['feet'] == 'left_fwd' else (-1 if p['feet'] == 'right_fwd' else 0))
    fr = 37 + (-1 if p['feet'] == 'left_fwd' else (2 if p['feet'] == 'right_fwd' else 0))
    feet = (ell(fl, 61.5, 5.2, 2.7) | ell(fr, 61.5, 5.2, 2.7)) & ~ovr
    fill(feet, P['lil_sh'])
    fill(feet & (yy_g == 63), P['lil_dp'])

    # 深棕剪影描边
    opaque = img[..., 3] > 0
    nb = np.zeros((S, S), bool)
    for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
        sh = np.zeros((S, S), bool)
        ys0, ys1 = max(dy, 0), S + min(dy, 0)
        xs0, xs1 = max(dx, 0), S + min(dx, 0)
        sh[ys0:ys1, xs0:xs1] = ~opaque[ys0 - dy: ys1 - dy, xs0 - dx: xs1 - dx]
        nb |= sh
    img[opaque & nb] = (*P['brown'], 255)
    return img

def checks(img):
    opaque = img[..., 3] > 0
    assert img.shape == (S, S, 4) and opaque.sum() > 800
    assert opaque[S - 1].sum() >= 4, 'feet must cover bottom row'
    rgb = img[..., :3].astype(int)
    cream = (np.abs(rgb - np.array(P['cream'])).sum(-1) < 30)
    lil = sum((np.abs(rgb - np.array(c)).sum(-1) < 30) for c in
              (P['lil_md'], P['lil_hi'], P['lil_dp']))
    bad = 0
    for y, x in zip(*np.where(cream)):
        for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < S and 0 <= nx < S and lil[ny, nx]:
                bad += 1
    assert bad == 0, f'cream-lilac leaks: {bad}'
    return True

if __name__ == '__main__':
    out = Path(__file__).resolve().parent
    img = draw_frame()
    checks(img)
    Image.fromarray(img).save(out / 'base-64.png')
    big = Image.fromarray(img).resize((S*4, S*4), Image.NEAREST)
    y2, x2 = np.mgrid[0:S*4, 0:S*4]
    g = (((y2//16) + (x2//16)) % 2 * 40 + 190).astype(np.uint8)
    ch = np.dstack([g, g, g])
    b = np.asarray(big).astype(np.float32)
    alb = b[..., 3:] / 255
    comp = (b[..., :3] * alb + ch * (1 - alb)).astype(np.uint8)
    Image.fromarray(comp).save(out / 'preview_4x.png')
    face = Image.fromarray(img).crop((10, 20, 54, 46)).resize((44*6, 26*6), Image.NEAREST)
    face.save(out / 'face_zoom_6x.png')
    print('base-64.png regenerated')

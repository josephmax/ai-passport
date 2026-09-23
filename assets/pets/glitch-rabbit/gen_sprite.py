# 宠物关键像素资产生成脚本：64x64 基础帧（站立正面）。
# 用法：python3 gen_sprite.py  ->  在本目录生成 base-64.png / preview_4x.png / face_zoom_6x.png
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
img = np.zeros((S, S, 4), np.uint8)
yy_g, xx_g = np.mgrid[0:S, 0:S]

def fill(m, c):
    img[m] = (*c, 255)

def ell(cx, cy, rx, ry):
    return ((xx_g - cx + 0.5) / rx) ** 2 + ((yy_g - cy + 0.5) / ry) ** 2 <= 1.0

def rot_ell(cx, cy, rx, ry, ang):
    dx = xx_g - cx + 0.5; dy = yy_g - cy + 0.5
    c, s = np.cos(ang), np.sin(ang)
    u = dx * c + dy * s; v = -dx * s + dy * c
    return (u / rx) ** 2 + (v / ry) ** 2 <= 1.0

def rrect(x0, y0, x1, y1):
    return (xx_g >= x0) & (xx_g < x1) & (yy_g >= y0) & (yy_g < y1)

# ---------- 长耳朵（圆头顶，不再截断） ----------
def ear(cx_top, cx_bot, w_top, w_bot, y_top, y_bot):
    m = ell(cx_top, y_top + 1.0, w_top * 0.95, 1.9)          # 圆头顶盖
    for i in range(37):
        t = i / 36
        cx = cx_bot + (cx_top - cx_bot) * t
        cy = y_bot + (y_top - y_bot) * t
        w = w_bot + (w_top - w_bot) * t
        if cy > y_top + 1.5:
            m |= ell(cx, cy, w, 1.0)
    return m
earL = ear(19.5, 26, 4.2, 5.6, 1, 17)
earR = ear(44.5, 38, 4.2, 5.6, 1, 17)
ears = earL | earR
fill(ears, P['lil_md'])
fill(earL & (xx_g < 17.5) & (yy_g >= 3) & (yy_g <= 9), P['lil_hi'])
fill(earR & (xx_g > 46.5) & (yy_g >= 3) & (yy_g <= 9), P['lil_hi'])
fill(earL & (xx_g > 23) & (yy_g < 15), P['lil_dp'])
fill(earR & (xx_g < 41) & (yy_g < 15), P['lil_dp'])

# ---------- 大圆头 ----------
head = ell(32, 28, 20, 16.5)
fill(head, P['lil_md'])
fill(head & ell(29, 25, 15, 11), P['lil_hi'])
fill(head & ~ell(29.5, 27, 18.8, 14.8), P['lil_dp'])   # 阴影只留右下缘

# ---------- 圆顶眼（照参考图：半月形、奶油黄楔形占左半、全高） ----------
domeL = ell(23.0, 27.2, 6.0, 3.3) & (yy_g <= 29.9) & (xx_g <= 29.0)
domeR = ell(41.0, 27.2, 6.0, 3.3) & (yy_g <= 29.9) & (xx_g >= 35.0)
wedL = domeL & (xx_g >= 24.4)   # 与右眼同构：瞳偏左、黄在右
wedR = domeR & (xx_g >= 42.4)
fill(domeL | domeR, P['brown'])
fill(wedL | wedR, P['cream'])
# 眼型闭合描边（参考图眼周有深棕轮廓圈）
def dilate4(m):
    d = np.zeros_like(m)
    d[1:, :] |= m[:-1, :]
    d[:-1, :] |= m[1:, :]
    d[:, 1:] |= m[:, :-1]
    d[:, :-1] |= m[:, 1:]
    return d
eye_ring = (dilate4(domeL | domeR) & ~(domeL | domeR)) & head
fill(eye_ring, P['brown'])

# ---------- 上翘大笑嘴（新月弧 + 满排牙） ----------
# 月牙嘴：两条圆弧相接于两尖角，亚像素采样保平滑
corner_y, half_w = 33.5, 14.5
s1, s2 = 1.5, 6.8                      # 上弧浅弯、下弧深弯（矢高）
r1 = half_w ** 2 / (2 * s1) + s1 / 2
cy1 = corner_y - (r1 - s1)
r2 = half_w ** 2 / (2 * s2) + s2 / 2
cy2 = corner_y - (r2 - s2)
dxg = xx_g - 32.0
in_w = np.abs(dxg) <= half_w
yu = cy1 + np.sqrt(np.clip(r1 ** 2 - dxg ** 2, 0, None))
yl = cy2 + np.sqrt(np.clip(r2 ** 2 - dxg ** 2, 0, None))
cov = np.zeros((S, S))
for ox in (0.125, 0.375, 0.625, 0.875):
    for oy in (0.125, 0.375, 0.625, 0.875):
        px = xx_g - 0.5 + ox; py = yy_g - 0.5 + oy
        dxp = px - 32.0
        yu_p = cy1 + np.sqrt(np.clip(r1 ** 2 - dxp ** 2, 0, None))
        yl_p = cy2 + np.sqrt(np.clip(r2 ** 2 - dxp ** 2, 0, None))
        cov += ((py > yu_p) & (py < yl_p) & (np.abs(dxp) <= half_w)) & head
cov /= 16.0
fill_m = head & (cov >= 0.5)
# 闭合描边：填充区的 4 邻域膨胀边界（保证不断线）
dil = np.zeros_like(fill_m)
dil[1:, :] |= fill_m[:-1, :]
dil[:-1, :] |= fill_m[1:, :]
dil[:, 1:] |= fill_m[:, :-1]
dil[:, :-1] |= fill_m[:, 1:]
ring = head & (dil & ~fill_m)
teeth = fill_m.copy()
sep = np.zeros_like(fill_m)
for x in range(20, 45, 4):
    sep |= rrect(x, 0, x + 1, S)
    teeth &= ~rrect(x, 0, x + 1, S)
fill(ring, P['brown'])
fill((dil | fill_m) & sep, P['brown'])   # 齿缝填棕（原来漏填露出底色）
fill(teeth, P['cream'])

# ---------- 瘦身躯干 ----------
body = ell(32, 52, 9.5, 10.5) & ~head
fill(body, P['lil_md'])
fill(body & ell(30.5, 49, 6, 5.5), P['lil_hi'])

# ---------- 手臂（肩到掌连续） + 奶油手掌 ----------
def limb(x0, y0, x1, y1, r):
    m = np.zeros((S, S), bool)
    for i in range(13):
        t = i / 12
        m |= ell(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r, r)
    return m
armL = limb(25.5, 47.5, 19.5, 54.5, 2.3) & ~head
armR = limb(38.5, 47.5, 44.5, 54.5, 2.3) & ~head
fill(armL | armR, P['lil_dp'])
pawL = ell(19, 55.5, 3.4, 3.1) & ~head
pawR = ell(45, 55.5, 3.4, 3.1) & ~head
fill(pawL | pawR, P['paw'])
fill((pawL & (xx_g < 17.5)) | (pawR & (xx_g > 46.5)), P['paw_sh'])

# ---------- 红背带裤 ----------
ovr = np.zeros((S, S), bool)
ovr |= rrect(24, 49, 40, 60) & ell(32, 52, 9.8, 10)
ovr |= rrect(27, 44, 37, 51) & ~head
fill(ovr, P['red_md'])
fill(ovr & (xx_g <= 26) & (yy_g <= 55), P['red_dp'])
fill(ovr & (yy_g >= 57), P['red_dp'])
for y in range(53, 60):
    px = np.zeros((S, S), bool); px[y, 32 - (y % 2)] = True
    fill(ovr & px, P['red_dp'])
strap = rrect(26, 41, 29, 45) | rrect(35, 41, 38, 45)
fill(strap & ~head, P['red_md'])
fill(strap & ~head & (xx_g <= 27), P['red_dp'])
fill(strap & ~head & (xx_g >= 37), P['red_dp'])
pk = rrect(28.5, 51, 35.5, 56) & ovr
fill(pk, P['red_dp'])
fill(rrect(28.5, 51, 35.5, 52) & ovr, P['red_md'])
hem = rrect(24, 58, 40, 60) & ell(32, 52, 10.2, 10.5)
fill(hem, P['red_dp'])

# ---------- 脚 ----------
feet = (ell(27, 61.5, 5.2, 2.7) | ell(37, 61.5, 5.2, 2.7)) & ~ovr
fill(feet, P['lil_sh'])
fill(feet & (yy_g == 63), P['lil_dp'])

# ---------- 深棕描边 ----------
opaque = img[..., 3] > 0
nb_tr = np.zeros((S, S), bool)
for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
    sh = np.zeros((S, S), bool)
    ys0, ys1 = max(dy, 0), S + min(dy, 0)
    xs0, xs1 = max(dx, 0), S + min(dx, 0)
    sh[ys0:ys1, xs0:xs1] = ~opaque[ys0 - dy: ys1 - dy, xs0 - dx: xs1 - dx]
    nb_tr |= sh
edge = opaque & nb_tr
img[edge] = (*P['brown'], 255)

out_dir = Path(__file__).resolve().parent
Image.fromarray(img).save(out_dir / 'base-64.png')
big = Image.fromarray(img).resize((S*4, S*4), Image.NEAREST)
y2, x2 = np.mgrid[0:S*4, 0:S*4]
g = (((y2//16) + (x2//16)) % 2 * 40 + 190).astype(np.uint8)
ch = np.dstack([g, g, g])
b = np.asarray(big).astype(np.float32)
alb = b[..., 3:] / 255
comp = (b[..., :3] * alb + ch * (1 - alb)).astype(np.uint8)
Image.fromarray(comp).save(out_dir / 'preview_4x.png')
face = Image.fromarray(img).crop((10, 20, 54, 46)).resize((44*6, 26*6), Image.NEAREST)
face.save(out_dir / 'face_zoom_6x.png')
print('opaque px:', int(opaque.sum()), 'bottom row:', int(opaque[63].sum()))

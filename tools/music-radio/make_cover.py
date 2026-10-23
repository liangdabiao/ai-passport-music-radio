# -*- coding: utf-8 -*-
"""生成 3:4 竖版封面（设计与渲染，不是实机截图）。"""
import math

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1152, 1536
FONT = "D:/esp/fontsrc/NotoSansCJKsc-Regular.otf"

BG_TOP = (26, 17, 11)
BG_BOT = (14, 9, 6)
AMBER = (242, 176, 74)
AMBER_DIM = (138, 98, 48)
CREAM = (245, 230, 200)
MUTED = (154, 138, 114)
POINTER = (255, 122, 60)


def font(size):
    return ImageFont.truetype(FONT, size)


def main():
    img = Image.new("RGB", (W, H), BG_TOP)
    d = ImageDraw.Draw(img)

    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)],
               fill=(int(BG_TOP[0] * (1 - t) + BG_BOT[0] * t),
                     int(BG_TOP[1] * (1 - t) + BG_BOT[1] * t),
                     int(BG_TOP[2] * (1 - t) + BG_BOT[2] * t)))

    glow = Image.new("RGB", (W, H), (0, 0, 0))
    ImageDraw.Draw(glow).ellipse([W * 0.08, H * 0.28, W * 0.92, H * 0.80],
                                 fill=(120, 62, 18))
    glow = glow.filter(ImageFilter.GaussianBlur(170))
    img = Image.blend(img, Image.blend(img, glow, 0.5), 0.5)
    d = ImageDraw.Draw(img)

    # 顶部
    d.text((W / 2, 92), "AI PASSPORT · 音乐电台",
           font=font(34), fill=MUTED, anchor="mm")

    # 主标题
    d.text((W / 2, 252), "4 万个电台", font=font(150), fill=AMBER, anchor="mm")
    d.text((W / 2, 372), "口袋里的全球收音机", font=font(58), fill=CREAM, anchor="mm")

    # 刻度盘面板
    panel = (96, 470, W - 96, 1046)
    d.rounded_rectangle(panel, radius=48, fill=(20, 12, 8),
                        outline=AMBER_DIM, width=5)

    cx, cy = W / 2, 806
    r_arc = 196
    # 刻度
    for i in range(21):
        t = i / 20
        ang = math.radians(200 + 140 * t)
        major = i % 4 == 0
        r_out = r_arc
        r_in = r_out - (56 if major else 30)
        d.line([(cx + r_out * math.cos(ang), cy + r_out * math.sin(ang)),
                (cx + r_in * math.cos(ang), cy + r_in * math.sin(ang))],
               fill=AMBER if major else AMBER_DIM, width=7 if major else 4)

    # 刻度数字（在弧外，避开刻度）
    for i, band in ((0, "88"), (10, "98"), (20, "108")):
        t = i / 20
        ang = math.radians(200 + 140 * t)
        rr = r_arc + 56
        d.text((cx + rr * math.cos(ang), cy + rr * math.sin(ang)), band,
               font=font(34), fill=MUTED, anchor="mm")

    # 指针
    ang = math.radians(200 + 140 * 0.62)
    d.line([(cx, cy), (cx + 170 * math.cos(ang), cy + 170 * math.sin(ang))],
           fill=POINTER, width=10)
    d.ellipse([cx - 19, cy - 19, cx + 19, cy + 19], fill=POINTER)
    d.ellipse([cx - 8, cy - 8, cx + 8, cy + 8], fill=(20, 12, 8))

    # 台名与状态（示意界面内容）
    d.text((cx, 900), "中国之声", font=font(88), fill=AMBER, anchor="mm")
    d.text((cx, 972), "正在播放 · 64 kbps", font=font(38), fill=MUTED, anchor="mm")

    # 卖点
    d.text((W / 2, 1120), "新闻 · 音乐 · 相声评书 · 故事 · 环球",
           font=font(46), fill=CREAM, anchor="mm")

    # 分隔与底部说明
    d.line([(240, 1200), (W - 240, 1200)], fill=AMBER_DIM, width=3)
    d.text((W / 2, 1264), "想听别的台？自己加就行",
           font=font(52), fill=AMBER, anchor="mm")
    d.text((W / 2, 1340), "电台数据来自 radio-browser.info",
           font=font(38), fill=MUTED, anchor="mm")
    d.text((W / 2, 1448), "OK 播放  ·  上下换台  ·  长按上下调音量",
           font=font(36), fill=MUTED, anchor="mm")

    img.save("build/cover_1152x1536.png", "PNG")
    img.save("build/cover_1152x1536.jpg", "JPEG", quality=93)
    print("cover written", img.size)


if __name__ == "__main__":
    main()

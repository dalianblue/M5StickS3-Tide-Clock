#!/usr/bin/env python3
"""
精确修复：只在原始文本中替换全零字节的值，不改变文本格式。
只替换尺寸完全一致的字符（子集字体和完整字体 Glyph 尺寸相同）。

使用方法：python3 fix_surgical.py
"""

import re

SUBSET_H = '/Users/yuzhang/adafruit-gfx-fontconvert/fontconvert/cnfont-subset16pt8b.h'
FULL_H = '/Users/yuzhang/adafruit-gfx-fontconvert/fontconvert/cnfont16pt7b.h'
OUTPUT_H = '/Users/yuzhang/M5StickS3/cnfont.h'


def parse_bitmaps(fname):
    with open(fname, 'r') as f:
        content = f.read()
    m = re.search(r'(Bitmaps\[\] PROGMEM = \{)(.*?)(\};)', content, re.DOTALL)
    bytes_text = m.group(2)
    byte_values = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', bytes_text)]
    return content, bytes_text, byte_values


def parse_glyphs(content):
    m = re.search(r'Glyphs\[\] PROGMEM = \{(.*?)\};', content, re.DOTALL)
    return [tuple(int(x) for x in g) for g in
            re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+),\s*(-?\d+)\s*\}', m.group(1))]


def main():
    # 读取子集和完整字体
    sub_content, sub_bm_text, sub_bm = parse_bitmaps(SUBSET_H)
    _, _, full_bm = parse_bitmaps(FULL_H)

    sub_gl = parse_glyphs(sub_content)
    full_gl = parse_glyphs(
        open(FULL_H).read())

    # 找出需要替换的字节：{ 子集offset → 完整字体offset }
    # 只替换尺寸一致的空字符
    replacements = {}
    fixed_chars = []
    skipped_chars = []

    for i, g in enumerate(sub_gl):
        offset, w, h, adv, xo, yo = g
        code = 0x20 + i
        if w <= 1 or h <= 1:
            continue
        size = ((w + 7) // 8) * h
        if offset + size > len(sub_bm):
            continue
        # 检查是否全零
        if not all(b == 0 for b in sub_bm[offset:offset + size]):
            continue  # 已有数据，跳过

        # 检查完整字体中同码点的 Glyph
        full_g = full_gl[i]
        f_offset, f_w, f_h = full_g[0], full_g[1], full_g[2]
        f_size = ((f_w + 7) // 8) * f_h

        if (w, h) == (f_w, f_h) and f_offset + f_size <= len(full_bm):
            # 尺寸一致，可以安全替换
            full_bitmap = full_bm[f_offset:f_offset + f_size]
            if any(b != 0 for b in full_bitmap):
                for j in range(size):
                    replacements[offset + j] = full_bitmap[j]
                try:
                    fixed_chars.append((chr(code), code, w, h))
                except ValueError:
                    pass
            else:
                skipped_chars.append((chr(code), code, '完整字体也空'))
        else:
            skipped_chars.append((chr(code), code, f'尺寸不一致 {w}x{h} vs {f_w}x{f_h}'))

    print(f"安全修复（尺寸一致）: {len(fixed_chars)} 个字符")
    for c, code, w, h in fixed_chars[:15]:
        print(f"  {c} (U+{code:04X}) {w}x{h}")
    if len(fixed_chars) > 15:
        print(f"  ... 还有 {len(fixed_chars) - 15} 个")

    print(f"\n跳过（尺寸不一致或完整字体也空）: {len(skipped_chars)} 个")
    for c, code, reason in skipped_chars[:10]:
        print(f"  {c} (U+{code:04X}): {reason}")

    # 在原始文本中精确替换字节（不改变格式）
    counter = [0]

    def replace_byte(m):
        idx = counter[0]
        counter[0] += 1
        if idx in replacements:
            return f'0x{replacements[idx]:02X}'
        return m.group(0)

    # 只替换 Bitmaps 区域
    bm_match = re.search(r'(Bitmaps\[\] PROGMEM = \{)(.*?)(\};)', sub_content, re.DOTALL)
    new_bm_text = re.sub(r'0x[0-9A-Fa-f]{2}', replace_byte, bm_match.group(2))

    # 拼接
    new_content = (sub_content[:bm_match.start(2)] +
                   new_bm_text +
                   sub_content[bm_match.end(2):])

    with open(OUTPUT_H, 'w') as f:
        f.write(new_content)

    import os
    print(f"\n已写入 {OUTPUT_H} ({os.path.getsize(OUTPUT_H)} 字节)")
    print("（原始文本格式完全保留，只替换了字节值）")


if __name__ == '__main__':
    main()

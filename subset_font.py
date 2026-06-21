#!/usr/bin/env python3
"""
精简字体脚本：从完整 TTF 提取项目用到的字符，输出精简 TTF + Adafruit GFX .h 文件

使用方法：
  1. 安装依赖：
     pip install fonttools
  2. 准备原始 TTF 文件（如 LXGWWenKai-Regular.ttf）
  3. 运行：
     python3 subset_font.py /path/to/LXGWWenKai-Regular.ttf

输出：
  - cnfont-subset.ttf（精简 TTF，只含 485 字符）
  - 然后用 Adafruit fontconvert 生成 .h 文件
"""

import sys
import os
import subprocess

def main():
    if len(sys.argv) < 2:
        print("用法: python3 subset_font.py <input.ttf>")
        print("示例: python3 subset_font.py LXGWWenKai-Regular.ttf")
        sys.exit(1)

    input_ttf = sys.argv[1]
    if not os.path.exists(input_ttf):
        print(f"错误：找不到文件 {input_ttf}")
        sys.exit(1)

    output_ttf = "cnfont-subset.ttf"
    chars_file = "chars.txt"

    if not os.path.exists(chars_file):
        print(f"错误：找不到 {chars_file}，请先在项目目录运行本脚本")
        sys.exit(1)

    # 1. 用 pyftsubset 子集化
    print(f"[1/2] 子集化 {input_ttf} → {output_ttf} ...")
    cmd = [
        "pyftsubset",
        input_ttf,
        f"--text-file={chars_file}",
        f"--output-file={output_ttf}",
        "--layout-features='*'",  # 保留所有 OpenType 特性
        "--no-hinting",            # 去除 hinting 减小体积
        "--desubroutinize",        # 优化 CFF
    ]
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        print("错误：未安装 pyftsubset，请运行: pip install fonttools")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        print(f"错误：子集化失败: {e}")
        sys.exit(1)

    size_before = os.path.getsize(input_ttf)
    size_after = os.path.getsize(output_ttf)
    print(f"  原始: {size_before:,} 字节 ({size_before/1024/1024:.1f} MB)")
    print(f"  精简: {size_after:,} 字节 ({size_after/1024:.1f} KB)")
    print(f"  缩减: {100 * (1 - size_after/size_before):.1f}%")

    # 2. 提示下一步
    print()
    print("[2/2] 用 Adafruit fontconvert 生成 .h：")
    print(f"  ./fontconvert {output_ttf} 16 0x20 0x9FFF")
    print()
    print("生成 cnfont16pt7b.h 后：")
    print("  cp cnfont16pt7b.h /Users/yuzhang/M5StickS3/cnfont.h")
    print("  # 然后在 config.h 取消注释 #define USE_CHINESE_FONT")
    print()
    print(f"输出文件: {output_ttf}")

if __name__ == "__main__":
    main()

import re
import os

# 读取原始头文件
with open('font_ds50.h', 'r') as f:
    content = f.read()

# 提取 Bitmaps 数组内容
bitmaps_match = re.search(r'const uint8_t DejaVu_Sans_50Bitmaps\[\] PROGMEM = \{\s*([\s\S]*?)\n\};', content)
bitmaps_data = bitmaps_match.group(1)

# 提取 Glyphs 表内容
glyphs_match = re.search(r'const GFXglyph DejaVu_Sans_50Glyphs\[\] PROGMEM = \{\s*([\s\S]*?)\n\};', content)
glyphs_text = glyphs_match.group(1)

# 解析 Glyphs 条目
glyph_entries = []
for line in glyphs_text.split('\n'):
    line = line.strip()
    if not line or line.startswith('//'):
        continue
    # 格式: { offset, width, height, xAdvance, xOffset, yOffset },
    m = re.match(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(-?\d+)\s*\},?', line)
    if m:
        offset, w, h, xAdv, xOff, yOff = map(int, m.groups())
        glyph_entries.append((offset, w, h, xAdv, xOff, yOff))

# 需要保留的字符及其 ASCII 码
keep_chars = [chr(ord('0')+i) for i in range(10)] + [':']
# 在 glyph_entries 中，顺序是空格(0x20), !, ", #, $, %, &, ', (, ), *, +, ,, -, ., /, 0-9, :, ;, ...
# 我们索引从 0 开始，所以 '0' 是第 16 个（空格为0，!为1，...），':' 是第 26 个
# 但我们直接按名称查找更可靠，但这里简化：因为我们知道顺序，但为了通用，我们可以通过字符映射
# 由于原始 ASCII 顺序连续，我们只保留 0x30-0x39 和 0x3A
# 索引：0x20=>0, 0x21=>1, ... 0x30=>16, 0x39=>25, 0x3A=>26
indices = [ord(c) - 0x20 for c in keep_chars]  # 空格索引0
new_glyphs = []
new_bitmaps = bytearray()
current_offset = 0

for idx in indices:
    if idx < len(glyph_entries):
        offset, w, h, xAdv, xOff, yOff = glyph_entries[idx]
        # 如果是冒号 (ASCII 0x3A)，调整 yOffset 向上移（更负）
        if idx == ord(':') - 0x20:  # 26
            yOff = -68  # 原为 -55，调高到 -68（与数字顶部更接近）
        # 提取对应的位图字节
        # 计算字节数
        byte_count = (w * h + 7) // 8
        # 从 bitmaps_data 中提取对应的十六进制字节
        # 需要解析 bitmaps_data 中的字节串
        # 这里简单做法：从原始文件中按偏移量查找，但更可靠是从 bitmaps_data 中按顺序读取
        # 由于我们已经有了 bitmaps_data 字符串，我们可以通过累计偏移来取
        # 但为了简化，我们直接从原数组中按偏移量提取，但需要知道每个字节的表示
        # 更好的方法：将 bitmaps_data 按逗号分割成字节列表
        bytes_list = re.findall(r'0x[0-9A-Fa-f]{2}', bitmaps_data)
        # 提取对应偏移的字节
        start = offset
        end = start + byte_count
        if end <= len(bytes_list):
            byte_vals = bytes_list[start:end]
            # 转为字节
            new_bitmaps.extend(int(b, 16) for b in byte_vals)
            # 更新偏移
            new_glyphs.append((current_offset, w, h, xAdv, xOff, yOff))
            current_offset += byte_count
        else:
            print(f"Warning: offset {offset} out of range for char {chr(0x20+idx)}")

# 生成新的头文件
new_bitmap_str = ', '.join(f'0x{b:02X}' for b in new_bitmaps)
new_glyph_str = ''
for g in new_glyphs:
    offset, w, h, xAdv, xOff, yOff = g
    new_glyph_str += f'\t{{ {offset:5d}, {w:2d}, {h:2d}, {xAdv:2d}, {xOff:2d}, {yOff:4d} }},\n'

new_content = f'''// Created by https://oleddisplay.squix.ch/ Consider a donation
// In case of problems make sure that you are using the font file with the correct version!
const uint8_t DejaVu_Sans_50Bitmaps[] PROGMEM = {{
\t{new_bitmap_str}
}};
const GFXglyph DejaVu_Sans_50Glyphs[] PROGMEM = {{
{new_glyph_str}
}};
const GFXfont DejaVu_Sans_50 PROGMEM = {{
(uint8_t  *)DejaVu_Sans_50Bitmaps,(GFXglyph *)DejaVu_Sans_50Glyphs,0x30, 0x3A, -119}};
'''

with open('font_ds50_small.h', 'w') as f:
    f.write(new_content)


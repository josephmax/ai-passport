// main/app/app_fmt.h —— 展示格式化(纯逻辑,产出 UTF-8 中文短串)。
// 所有串的字形必须在中文字体子集清单内(见 tools/fonts/glyphs.txt)。
#pragma once

#include <stddef.h>
#include <stdint.h>

// 相对时间:"刚刚" / "x分钟前" / "x小时前" / "x天前"。
void app_fmt_ago(int64_t now_ms, int64_t at_ms, char *buf, size_t cap);

// Token 数值紧凑化:>=1万 用 "41.2万",否则原值千分位不加分隔符的短形式。
void app_fmt_tokens(double value, char *buf, size_t cap);

// 计时剩余 mm:ss(可超 60 分,如 "124:59");负数按 0 处理。
void app_fmt_clock(int64_t remaining_ms, char *buf, size_t cap);

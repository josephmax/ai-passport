// main/app/app_audio_fx.h —— 胜利音效/提示音播放(独立任务,音量随设置)。
// 按键回调只调 queue 接口;真正的 I2S 写入在本模块的任务里完成。
// 与同步任务互斥:同步进行中直接丢弃播放请求(规格 §11 红线)。
#pragma once

#include <stdbool.h>

typedef enum {
    APP_FX_VICTORY = 0,   // 胜利动画配乐
    APP_FX_BEEP,          // 堆满/不可操作提示
} app_fx_t;

// 初始化 codec(幂等)并启动播放任务。失败返回 false(静音降级)。
bool app_audio_fx_init(void);

// 异步播放;忙时新请求替换旧的(胜利优先于提示)。
void app_audio_fx_play(app_fx_t fx);

// 立即中止当前播放(任意按键终止音效,规格 §6.1 用户定稿)。
void app_audio_fx_stop(void);

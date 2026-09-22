// main/app/app_audio_fx.c —— 音效实现:EMBED PCM → 音量缩放 → ES8311。
#include "app_audio_fx.h"

#include "app_debug.h"
#include "app_runtime.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stddef.h>
#include <stdint.h>

static const char *TAG = "audio_fx";

extern const uint8_t pendant_victory_pcm_start[] asm("_binary_pendant_victory_pcm_start");
extern const uint8_t pendant_victory_pcm_end[] asm("_binary_pendant_victory_pcm_end");
extern const uint8_t pendant_beep_pcm_start[] asm("_binary_pendant_beep_pcm_start");
extern const uint8_t pendant_beep_pcm_end[] asm("_binary_pendant_beep_pcm_end");

#define SAMPLE_RATE_HZ 16000
#define CHUNK_SAMPLES 512

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

// 档位 0..5 → 输出百分比;0 档静音但流程照走(时序不变,好测试)。
static const uint16_t VOLUME_PERCENT[6] = { 0, 12, 25, 40, 60, 85 };

static void play_pcm(const int16_t *pcm, size_t samples, uint8_t volume) {
    if (bsp_audio_wake() != ESP_OK) return;
    if (bsp_audio_set_format(SAMPLE_RATE_HZ, 16, 1) != ESP_OK) goto done;
    bsp_audio_set_volume(VOLUME_PERCENT[volume % 6]);

    int16_t chunk[CHUNK_SAMPLES];
    while (samples > 0) {
        size_t n = samples < CHUNK_SAMPLES ? samples : CHUNK_SAMPLES;
        for (size_t i = 0; i < n; i++) {
            // 定点缩放:16bit 样本 × 档位百分比,饱和收敛。
            int32_t v = (pcm[i] * VOLUME_PERCENT[volume % 6]) / 100;
            chunk[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        }
        if (bsp_audio_write(chunk, n * sizeof(int16_t)) != ESP_OK) break;
        pcm += n;
        samples -= n;
    }
done:
    // 播完即挂起 codec,省电;下次播放 wake 恢复。
    (void)bsp_audio_sleep();
}

static void fx_task(void *arg) {
    (void)arg;
    app_fx_t fx;
    for (;;) {
        if (xQueueReceive(s_queue, &fx, portMAX_DELAY) != pdTRUE) continue;
        // 同步期间不播(规格 §11:TLS 握手峰值不与音频叠加)。
        if (app_runtime()->sync_in_progress) continue;

        app_runtime_t *rt = app_runtime();
        if (fx == APP_FX_VICTORY) {
            play_pcm((const int16_t *)pendant_victory_pcm_start,
                     (pendant_victory_pcm_end - pendant_victory_pcm_start) / 2,
                     rt->settings.volume);
        } else {
            play_pcm((const int16_t *)pendant_beep_pcm_start,
                     (pendant_beep_pcm_end - pendant_beep_pcm_start) / 2,
                     rt->settings.volume);
        }
    }
}

bool app_audio_fx_init(void) {
    if (s_task) return true;
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败,静音降级: %s", esp_err_to_name(err));
        return false;
    }
    (void)bsp_audio_sleep();   // 待机,首次播放再唤醒
    s_queue = xQueueCreate(2, sizeof(app_fx_t));
    if (!s_queue) return false;
    bool ok = xTaskCreate(fx_task, "audio_fx", 3072, NULL, 4, &s_task) == pdPASS;
    if (ok) app_debug_register("audio_fx", s_task);
    if (!ok) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return false;
    }
    return true;
}

void app_audio_fx_play(app_fx_t fx) {
    if (!s_queue) return;
    xQueueReset(s_queue);          // 后到覆盖先到
    (void)xQueueSend(s_queue, &fx, 0);
}

// main/app/app_net.c —— Wi-Fi STA 实现。
#include "app_net.h"

#include "app_runtime.h"
#include "app_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "net";
static EventGroupHandle_t s_events;
#define BIT_GOT_IP (1 << 0)

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    app_runtime()->wifi_connected = true;
    xEventGroupSetBits(s_events, BIT_GOT_IP);
    app_runtime_publish(APP_EVENT_WIFI_STATE);
}

static void on_disconnect(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data;
    app_runtime()->wifi_connected = false;
    app_runtime_publish(APP_EVENT_WIFI_STATE);
    // 凭证存在则让系统自动重连;失败原因在事件数据里,不刷屏。
    wifi_event_sta_disconnected_t *info = data;
    if (info && info->reason == WIFI_REASON_ASSOC_LEAVE) return;
    ESP_LOGW(TAG, "Wi-Fi 断开,自动重连");
}

bool app_net_init(void) {
    if (s_events) return true;
    s_events = xEventGroupCreate();
    if (!s_events) return false;

    // 全链路优雅降级:任何一步失败都保持离线运行(设置页可再试配网),
    // 绝不 abort —— 首刷曾因素材占满堆导致 esp_wifi_init NO_MEM 循环重启。
    // 事件循环由 app_main 最早创建,这里只做 Wi-Fi 栈自身的降级初始化。
    esp_err_t err = esp_netif_init();
    if (err == ESP_OK && !esp_netif_create_default_wifi_sta()) err = ESP_ERR_NO_MEM;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (err == ESP_OK) err = esp_wifi_init(&cfg);
    if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         on_ip, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                         on_disconnect, NULL);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi 栈初始化失败,离线运行: %s", esp_err_to_name(err));
        return false;
    }

    app_net_cfg_t net;
    if (app_store_net(&net) && net.ssid[0]) {
        wifi_config_t wc = { 0 };
        strlcpy((char *)wc.sta.ssid, net.ssid, sizeof(wc.sta.ssid));
        strlcpy((char *)wc.sta.password, net.password, sizeof(wc.sta.password));
        if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK ||
            esp_wifi_start() != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi 启动失败,离线运行");
            return false;
        }
        ESP_LOGI(TAG, "Wi-Fi 连接中: %s", net.ssid);
    } else {
        ESP_LOGI(TAG, "无 Wi-Fi 凭证,保持离线(配网见设置页)");
        if (esp_wifi_start() != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi 启动失败,离线运行");
            return false;
        }
    }

    return true;
}

bool app_net_connected(void) {
    return s_events &&
           (xEventGroupGetBits(s_events) & BIT_GOT_IP) != 0;
}

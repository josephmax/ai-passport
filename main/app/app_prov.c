// main/app/app_prov.c —— SoftAP 配网实现。
//
// 并发模型:httpd 回调只解析表单、投队列、回响应;Wi-Fi 切换/等 IP/
// 配对都在 prov_task(worker)里完成 —— 回调绝不做慢操作,也不在自身
// handler 里停 httpd(会死锁)。
#include "app_prov.h"

#include "app_net.h"
#include "app_runtime.h"
#include "app_store.h"
#include "app_sync.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "prov";
#define AP_IP_LAST_OCTET 1
#define DNS_PORT 53

typedef struct {
    app_net_cfg_t net;
    char code[8];
} prov_form_t;

static volatile app_prov_state_t s_state = APP_PROV_IDLE;
static char s_ap_name[24];
static httpd_handle_t s_httpd;
static int s_dns_sock = -1;
static TaskHandle_t s_dns_task;
static TaskHandle_t s_task;
static QueueHandle_t s_queue;
static volatile bool s_start_requested;

static const char PAGE_FORM[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n<!DOCTYPE html><html><head>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>AI Passport</title></head><body style='font-family:sans-serif;"
    "max-width:420px;margin:40px auto;padding:0 16px'>"
    "<h2>AI Passport</h2>"
    "<form method='POST' action='/save'>"
    "<p>Wi-Fi 名称<br><input name=ssid required maxlength=32 style='width:100%'></p>"
    "<p>Wi-Fi 密码<br><input name=pass type=password maxlength=64 style='width:100%'></p>"
    "<p>服务地址<br><input name=url placeholder='http://192.168.1.10:3000' "
    "required maxlength=90 style='width:100%'></p>"
    "<p>配对码(6位)<br><input name=code pattern='[0-9]{6}' required "
    "maxlength=6 style='width:100%'></p>"
    "<p><button style='width:100%;padding:12px'>保存并连接</button></p>"
    "</form></body></html>";

static const char PAGE_SAVED[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
    "Connection: close\r\n\r\n<!DOCTYPE html><html><head>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "</head><body style='font-family:sans-serif;text-align:center;"
    "margin-top:60px'><h2>已收到</h2>"
    "<p>设备正在连接 Wi-Fi 并配对,请看设备屏幕。可断开此热点。</p>"
    "</body></html>";

static void url_decode(char *s) {
    char *o = s;
    while (*s) {
        if (*s == '%' && s[1] && s[2]) {
            int v;
            if (sscanf(s + 1, "%2x", &v) == 1) {
                *o++ = (char)v;
                s += 3;
                continue;
            }
        }
        *o++ = (*s == '+') ? ' ' : *s;
        s++;
    }
    *o = '\0';
}

static bool form_field(const char *body, const char *key, char *out, size_t cap) {
    char pat[16];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    p += strlen(pat);
    const char *e = strchr(p, '&');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    if (n >= cap) n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    url_decode(out);
    return out[0] != '\0';
}

static esp_err_t portal_get(httpd_req_t *req) {
    httpd_resp_send(req, PAGE_FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t portal_post(httpd_req_t *req) {
    char body[512] = "";
    int total = req->content_len;
    if (total > (int)sizeof(body) - 1) total = (int)sizeof(body) - 1;
    int got = httpd_req_recv(req, body, total);
    if (got <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, NULL);
        return ESP_FAIL;
    }
    body[got] = '\0';

    prov_form_t form = { 0 };
    bool ok = form_field(body, "ssid", form.net.ssid, sizeof(form.net.ssid)) &&
              form_field(body, "url", form.net.service_url,
                         sizeof(form.net.service_url)) &&
              form_field(body, "code", form.code, sizeof(form.code));
    form_field(body, "pass", form.net.password, sizeof(form.net.password));

    httpd_resp_send(req, PAGE_SAVED, HTTPD_RESP_USE_STRLEN);
    if (ok && s_queue) {
        xQueueSend(s_queue, &form, 0);   // 慢活交给 prov_task
    }
    return ESP_OK;
}

static httpd_uri_t URI_GET = { .uri = "/", .method = HTTP_GET, .handler = portal_get };
static httpd_uri_t URI_POST = { .uri = "/save", .method = HTTP_POST, .handler = portal_post };

// ---- 最小 DNS:所有 A 查询应答本机 AP IP(captive 探测域名全劫持) ----
static void dns_task(void *arg) {
    (void)arg;
    uint8_t pkt[160];
    struct sockaddr_in src;
    for (;;) {
        socklen_t slen = sizeof(src);
        int n = recvfrom(s_dns_sock, pkt, sizeof(pkt), 0,
                         (struct sockaddr *)&src, &slen);
        if (n < 0) break;   // socket 已关,退出
        if (n < 12) continue;
        int q_end = 12;
        while (q_end < n && pkt[q_end] != 0) q_end += pkt[q_end] + 1;
        q_end += 5;
        if (q_end + 16 > (int)sizeof(pkt)) continue;
        uint8_t *r = pkt + q_end;
        r[0] = 0xC0; r[1] = 0x0C;
        r[2] = 0; r[3] = 1;
        r[4] = 0; r[5] = 1;
        r[6] = 0; r[7] = 0; r[8] = 0; r[9] = 60;
        r[10] = 0; r[11] = 4;
        r[12] = 192; r[13] = 168; r[14] = 4; r[15] = AP_IP_LAST_OCTET;
        pkt[2] |= 0x80;
        pkt[7] = 1;
        sendto(s_dns_sock, pkt, q_end + 16, 0,
               (struct sockaddr *)&src, sizeof(src));
    }
    vTaskDelete(NULL);
}

static void dns_stop(void) {
    if (s_dns_sock >= 0) {
        shutdown(s_dns_sock, 0);
        closesocket(s_dns_sock);
        s_dns_sock = -1;
    }
    if (s_dns_task) {
        vTaskDelete(s_dns_task);   // recvfrom 无超时,直接回收
        s_dns_task = NULL;
    }
}

static void ap_services_stop(void) {
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    dns_stop();
}

// ---- worker:收表单/启动请求 → 切 STA → 连接 → 配对 ----
static void prov_task(void *arg) {
    (void)arg;
    prov_form_t form;
    for (;;) {
        if (xQueueReceive(s_queue, &form, pdMS_TO_TICKS(500)) != pdTRUE) {
            if (s_start_requested) {
                s_start_requested = false;
                app_prov_start();
            }
            continue;
        }
        s_state = APP_PROV_CONNECTING;
        ap_services_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
        app_store_save_net(&form.net);
        ESP_LOGI(TAG, "凭证已存,连接 %s", form.net.ssid);

        esp_wifi_stop();
        wifi_config_t wc = { 0 };
        strlcpy((char *)wc.sta.ssid, form.net.ssid, sizeof(wc.sta.ssid));
        strlcpy((char *)wc.sta.password, form.net.password, sizeof(wc.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wc);
        esp_wifi_start();

        bool got_ip = false;
        for (int i = 0; i < 30 && !got_ip; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            got_ip = app_net_connected();
        }
        if (!got_ip) {
            ESP_LOGW(TAG, "Wi-Fi 连接失败,回到配网热点");
            s_state = APP_PROV_FAILED;
            app_prov_start();
            continue;
        }

        s_state = APP_PROV_PAIRING;
        if (app_sync_pair(form.net.service_url, form.code)) {
            s_state = APP_PROV_DONE;
            app_sync_request_now();
            ESP_LOGI(TAG, "配网完成");
        } else {
            s_state = APP_PROV_FAILED;
            app_prov_start();
        }
    }
}

bool app_prov_start(void) {
    if (s_state == APP_PROV_AP_UP || s_state == APP_PROV_CONNECTING ||
        s_state == APP_PROV_PAIRING) {
        return true;
    }
    if (!s_queue) {
        s_queue = xQueueCreate(2, sizeof(prov_form_t));
        if (!s_queue) return false;
    }
    if (!s_task &&
        xTaskCreate(prov_task, "prov", 4096, NULL, 4, &s_task) != pdPASS) {
        return false;
    }

    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(s_ap_name, sizeof(s_ap_name), "Passport-%02X%02X", mac[4], mac[5]);

    esp_wifi_stop();
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;
    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, s_ap_name, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_ap_name);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;   // P1:一次性短窗口,开网简化
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (esp_wifi_start() != ESP_OK) return false;

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock >= 0) {
        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(DNS_PORT);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        int reuse = 1;
        setsockopt(s_dns_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(s_dns_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            xTaskCreate(dns_task, "prov_dns", 2560, NULL, 4, &s_dns_task);
        } else {
            dns_stop();
        }
    }

    if (!s_httpd) {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.max_uri_handlers = 8;
        if (httpd_start(&s_httpd, &cfg) == ESP_OK) {
            httpd_register_uri_handler(s_httpd, &URI_GET);
            httpd_register_uri_handler(s_httpd, &URI_POST);
        } else {
            s_httpd = NULL;
        }
    }

    s_state = APP_PROV_AP_UP;
    ESP_LOGI(TAG, "配网热点就绪: %s(开放)", s_ap_name);
    return s_httpd != NULL;
}

void app_prov_cancel(void) {
    if (s_state == APP_PROV_IDLE) return;
    ap_services_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    // 已存凭证则回到 STA 重连;wakeup 由 app_net 的自动重连接管。
    s_state = APP_PROV_IDLE;
}

app_prov_state_t app_prov_state(void) {
    return s_state;
}

const char *app_prov_ap_name(void) {
    return s_state == APP_PROV_IDLE ? NULL : s_ap_name;
}

void app_prov_request_start(void) {
    // 队列/任务不存在时(理论上不会)同步启动兜底。
    if (!s_task) {
        app_prov_start();
        return;
    }
    s_start_requested = true;
}

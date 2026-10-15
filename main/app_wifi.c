/**
 * @file     app_wifi.c
 * @brief    WiFi 配网业务层
 *
 * 功能：配网事件回调、状态查询；纯 WiFi，不依赖功放/TTS，
 *       音频播报由上层通过 app_wifi_set_notify_cb() 订阅事件后自行处理。
 * 修改：2026-10-06 合并 wifi_prov，改用 wifi_manager API
 * 修改：2026-10-14 去耦：移除 max98357/tts_hub 依赖，
 *       新增通知回调转发，播报逻辑下沉到 app_voice
 */
#include "esp_log.h"
#include "wifi_manager.h"
#include "app_wifi.h"

static const char *TAG = "WIFI_APP";

/* 上层通知回调：app_voice 注册进来收 WiFi 事件，app_wifi 本身不碰音频 */
static app_wifi_notify_cb_t s_notify_cb = NULL;
static void *s_notify_ctx = NULL;

void app_wifi_set_notify_cb(app_wifi_notify_cb_t cb, void *ctx)
{
    s_notify_cb = cb;
    s_notify_ctx = ctx;
}

static void on_wifi_event(wifi_mgr_event_t event, void *ctx)
{
    (void)ctx;
    switch (event) {
    case WIFI_MGR_EV_AP_STARTED:
        ESP_LOGW(TAG, "配网模式已启动：连上热点后打开浏览器");
        break;

    case WIFI_MGR_EV_CONNECTING:
        ESP_LOGI(TAG, "正在连接已保存的 WiFi …");
        break;

    case WIFI_MGR_EV_CONNECTED:
        ESP_LOGI(TAG, "已关联热点，等待获取 IP …");
        break;

    case WIFI_MGR_EV_GOT_IP: {
        char ip[16] = { 0 };
        wifi_status_t st;
        if (wifi_manager_get_status(&st) == ESP_OK) {
            strncpy(ip, st.ip_address, sizeof(ip) - 1);
        }
        ESP_LOGI(TAG, "WiFi 已连接，IP=%s", ip);
        break;
    }

    case WIFI_MGR_EV_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi 已断开");
        break;

    case WIFI_MGR_EV_FALLBACK:
        ESP_LOGW(TAG, "连接失败，已进入配网模式");
        break;

    default:
        break;
    }
    /* 转发给上层（如 app_voice 做播报），本层不直接播音 */
    if (s_notify_cb) {
        s_notify_cb(event, s_notify_ctx);
    }
}

void app_wifi_init(void)
{
    wifi_manager_config_t cfg = {
        .mode              = WIFI_MGR_MODE_AUTO,
        .max_retry         = 5,
        .retry_interval_ms = 3000,
        .auto_reconnect    = true,
        .enable_ntp        = true,
        .ntp_server        = "cn.pool.ntp.org",
        .fallback_ssid     = "XIAOLE_WIFI",
        .fallback_pw       = "12345678",
        .fallback_channel  = 6,
        .event_cb          = on_wifi_event,
        .event_cb_ctx      = NULL,
    };
    ESP_ERROR_CHECK(wifi_manager_init(&cfg));
}

bool app_wifi_is_connected(void)
{
    return wifi_manager_is_connected();
}

void app_wifi_get_ip(char *buf, uint32_t len)
{
    if (buf == NULL || len == 0) return;
    wifi_status_t st;
    if (wifi_manager_get_status(&st) == ESP_OK) {
        strncpy(buf, st.ip_address, len - 1);
        buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
}

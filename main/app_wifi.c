/**
 * @file     app_wifi.c
 * @brief    WiFi 配网业务层
 *
 * 功能：配网事件回调、状态查询、未来 UI/TTS 接入点
 * 修改：2026-10-03 新建（从 main.c 拆出 on_wifi_event）
 *
 * @note     当前阶段事件仅打印日志，等阶段 5 (字体) 与阶段 7 (TTS) 就绪后
 *          在此接入 0.96" 屏显示与语音播报。
 */
#include "esp_log.h"

#include "app_wifi.h"

static const char *TAG = "WIFI_APP";

static wifi_prov_state_t s_state = WIFI_PROV_STATE_IDLE;

/**
 * @brief  配网事件回调
 *
 * 功能：把 wifi_prov 的事件转为日志，更新内部状态机
 * 修改：2026-10-03 从 main.c 剪出，加入状态机更新
 *
 * @note   对应需求文档 4.3 的第 5、6 步：
 *         配网成功后 0.96" 屏显示状态、扩音器播报提示。
 *         当前阶段仅打印 + 状态保存。
 */
static void on_wifi_event(wifi_prov_event_t event, void *ctx)
{
    (void)ctx;

    switch (event) {
    case WIFI_PROV_EVENT_AP_STARTED:
        s_state = WIFI_PROV_STATE_PORTAL;
        ESP_LOGW(TAG, "进入配网模式：手机连上 XIAOLE_WIFI（密码 12345678）即可自动弹出配网页");
        break;

    case WIFI_PROV_EVENT_CONNECTING:
        s_state = WIFI_PROV_STATE_CONNECTING;
        ESP_LOGI(TAG, "正在连接已保存的 WiFi ...");
        break;

    case WIFI_PROV_EVENT_CONNECTED:
        ESP_LOGI(TAG, "已关联热点，等待获取 IP ...");
        break;

    case WIFI_PROV_EVENT_GOT_IP:
        s_state = WIFI_PROV_STATE_CONNECTED;
        do {
            char ip[16] = { 0 };
            wifi_prov_get_ip(ip, sizeof(ip));
            ESP_LOGI(TAG, "WiFi 已连接，IP=%s", ip);
        } while (0);
        break;

    case WIFI_PROV_EVENT_DISCONNECTED:
        if (s_state != WIFI_PROV_STATE_PORTAL) {
            s_state = WIFI_PROV_STATE_FAILED;
        }
        ESP_LOGW(TAG, "WiFi 已断开");
        break;

    case WIFI_PROV_EVENT_FAILED:
        s_state = WIFI_PROV_STATE_FAILED;
        ESP_LOGW(TAG, "连接失败，已回落配网模式");
        break;

    default:
        break;
    }
}

void app_wifi_init(void)
{
    wifi_prov_set_callback(on_wifi_event, NULL);
    ESP_ERROR_CHECK(wifi_prov_init());
}

wifi_prov_state_t app_wifi_get_state(void)
{
    return s_state;
}

bool app_wifi_is_connected(void)
{
    return wifi_prov_is_connected();
}

void app_wifi_get_ip(char *buf, uint32_t len)
{
    wifi_prov_get_ip(buf, len);
}
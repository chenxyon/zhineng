/**
 * @file     wifi_prov.c
 * @brief    WiFi 配网业务适配层实现
 *
 * 功能：封装 wifi_manager 组件，对外提供状态查询、事件回调与手动配网入口
 * 修改：2026-10-03 新建；start_portal() 按需求文档 4.3 修正为「先停 Web 服务 ->
 *         再停 SoftAP -> 再进配网模式」，旧配置清理移到配网启动成功之后，
 *         避免中途失败导致配置已丢
 * 修改：2026-10-04 业务层事件监听移到 wifi_manager_init() 之后注册。原先在其之前
 *         注册时 WIFI_EVENT 事件基尚未创建，esp_event_handler_register 返回
 *         ESP_ERR_INVALID_STATE，经 ESP_ERROR_CHECK 触发 abort 重启；
 *         且监听注册失败现降级为日志告警，不再致命
 *
 * @note     实现依据：需求文档 v3.0 第四章
 *          状态机本身由 wifi_manager 内部处理（重试计数、失败回落 SoftAP），
 *          本文件只做 4.3 的收尾顺序与事件转发，不重复实现状态机。
 */

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

#include "wifi_prov.h"

static const char *TAG = "WIFI_PROV";

static wifi_prov_state_t s_state       = WIFI_PROV_STATE_IDLE;
static wifi_prov_cb_t    s_cb          = NULL;
static void             *s_cb_ctx      = NULL;

/**
 * @brief  派发事件给业务层
 *
 * 功能：统一出口，避免各处重复判空
 * 修改：2026-10-03 新建
 */
static void prov_emit(wifi_prov_event_t event)
{
    if (s_cb != NULL) {
        s_cb(event, s_cb_ctx);
    }
}

/**
 * @brief  WiFi 与 IP 事件处理
 *
 * 功能：把组件的底层事件翻译成本层的配网事件
 * 修改：2026-10-03 新建
 *
 * @note   配网成功判据严格按需求文档 4.4：必须收到 IP_EVENT_STA_GOT_IP，
 *         不能仅凭 WIFI_EVENT_STA_CONNECTED。
 */
static void prov_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            s_state = WIFI_PROV_STATE_CONNECTING;
            prov_emit(WIFI_PROV_EVENT_CONNECTING);
            break;

        case WIFI_EVENT_STA_CONNECTED:
            /* 已关联热点，但尚未拿到 IP，暂不判定为成功 */
            ESP_LOGI(TAG, "已关联热点，等待获取 IP");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            s_state = WIFI_PROV_STATE_CONNECTING;
            prov_emit(WIFI_PROV_EVENT_DISCONNECTED);
            break;

        case WIFI_EVENT_AP_START:
            s_state = WIFI_PROV_STATE_PORTAL;
            ESP_LOGI(TAG, "SoftAP 与配网页面已就绪");
            prov_emit(WIFI_PROV_EVENT_AP_STARTED);
            break;

        default:
            break;
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        /* 需求文档 4.4：拿到 IP 才算连接成功 */
        s_state = WIFI_PROV_STATE_CONNECTED;
        wifi_status_t status;
        if (wifi_get_status(&status) == ESP_OK) {
            ESP_LOGI(TAG, "连接成功，IP=%s RSSI=%d", status.ip_address, status.rssi);
        }
        prov_emit(WIFI_PROV_EVENT_GOT_IP);
    }
}

esp_err_t wifi_prov_init(void)
{
    /* NVS 是 wifi_manager 存配置的前提，必须先初始化 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区异常，擦除重建");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 先初始化组件：WiFi 驱动与事件基由组件内部创建，
       业务层监听必须在其之后注册，否则 esp_event_handler_register
       会返回 ESP_ERR_INVALID_STATE 并被 ESP_ERROR_CHECK 触发重启 */
    wifi_manager_config_t cfg = {
        .retry_config = {
            .max_retry_count   = 5,
            .retry_interval_ms = 3000,
        },
        .time_sync_config = {
            .enabled            = true,
            .sync_interval_min  = 60,
            .ntp_server         = "cn.pool.ntp.org",
        },
        .auto_reconnect       = true,
        .default_config_mode = WIFI_CONFIG_MODE_WEB,
        .softap_config = {
            .ssid     = "XIAOLE_WIFI",
            .password = "12345678",
            .channel  = 6,
            .hidden   = false,
        },
    };

    ESP_ERROR_CHECK(wifi_manager_init(&cfg));

    /* 组件初始化完成后再注册业务层监听：
       esp_event_handler_register 失败不应导致重启，降级为日志告警 */
    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   prov_event_handler, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "WIFI_EVENT 监听注册失败，状态跟踪将不完整");
    }
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                   prov_event_handler, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "IP_EVENT 监听注册失败，状态跟踪将不完整");
    }

    ESP_LOGI(TAG, "配网子系统就绪");
    return ESP_OK;
}

void wifi_prov_set_callback(wifi_prov_cb_t cb, void *ctx)
{
    s_cb     = cb;
    s_cb_ctx = ctx;
}

wifi_prov_state_t wifi_prov_get_state(void)
{
    return s_state;
}

bool wifi_prov_is_connected(void)
{
    return s_state == WIFI_PROV_STATE_CONNECTED;
}

esp_err_t wifi_prov_start_portal(void)
{
    ESP_LOGI(TAG, "手动进入配网模式");

    /* 需求文档 4.3：先停配网页面服务，再停 SoftAP，避免驱动状态错乱 */
    wifi_webserver_stop();
    wifi_manager_softap_stop();

    esp_err_t err = wifi_config_mode_start(WIFI_CONFIG_MODE_WEB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "进入配网模式失败：%s", esp_err_to_name(err));
        return err;
    }

    /* 进入配网后清掉旧配置：否则下次上电会走 STA 直连而非配网页面 */
    wifi_config_clear();
    s_state = WIFI_PROV_STATE_PORTAL;
    ESP_LOGI(TAG, "配网模式已启动");
    return ESP_OK;
}

esp_err_t wifi_prov_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_saved_config_t cfg = {
        .saved   = true,
    };
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    if (password != NULL) {
        strncpy(cfg.password, password, sizeof(cfg.password) - 1);
    }

    ESP_ERROR_CHECK(wifi_config_save(&cfg));
    ESP_LOGI(TAG, "已保存配置，开始连接 %s", cfg.ssid);

    s_state = WIFI_PROV_STATE_CONNECTING;
    return wifi_connect();
}

void wifi_prov_get_ip(char *buf, uint32_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }

    wifi_status_t status;
    if (wifi_get_status(&status) == ESP_OK) {
        strncpy(buf, status.ip_address, len - 1);
        buf[len - 1] = '\0';
    } else {
        buf[0] = '\0';
    }
}
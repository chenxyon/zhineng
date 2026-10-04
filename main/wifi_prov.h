/**
 * @file     wifi_prov.h
 * @brief    WiFi 配网业务适配层
 *
 * 功能：把 wifi_manager 组件的状态变化转为 UI/语音事件，封装需求文档 4.3 的收尾顺序
 * 修改：2026-10-03 新建
 * 修改：2026-10-03 精简为适配层——状态机已由 wifi_manager 内部实现，不重复造
 *
 * @note     需求分工：
 *          - 配网状态机（重试计数、失败回落 SoftAP、Captive Portal）
 *            已由 d:\components\wifi_manager 内部实现，见 wifi_manager.c
 *            的 wifi_event_handler()。
 *          - 本文件只负责需求文档 4.3 要求的收尾顺序
 *            （先停 Web 再停 SoftAP）与对外事件通知。
 */
#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 配网状态
 *
 * 功能：对外暴露的配网阶段，供 UI 与业务层查询
 * 修改：2026-10-03 新增
 */
typedef enum {
    WIFI_PROV_STATE_IDLE = 0,     /*!< 未初始化 */
    WIFI_PROV_STATE_CONNECTING,   /*!< 正在连接已保存的 WiFi */
    WIFI_PROV_STATE_PORTAL,       /*!< SoftAP 配网模式，等待用户提交 */
    WIFI_PROV_STATE_CONNECTED,    /*!< 已获取 IP，连接成功 */
    WIFI_PROV_STATE_FAILED,       /*!< 连接失败已达上限，已回落配网模式 */
} wifi_prov_state_t;

/**
 * 配网事件
 *
 * 功能：状态变化时通知业务层，用于更新屏幕与语音播报
 * 修改：2026-10-03 新增
 */
typedef enum {
    WIFI_PROV_EVENT_AP_STARTED = 0, /*!< SoftAP 与配网页面已就绪 */
    WIFI_PROV_EVENT_CONNECTING,     /*!< 开始连接目标 WiFi */
    WIFI_PROV_EVENT_CONNECTED,      /*!< 已关联到热点 */
    WIFI_PROV_EVENT_GOT_IP,         /*!< 已获取 IP，连接成功 */
    WIFI_PROV_EVENT_DISCONNECTED,   /*!< 连接断开 */
    WIFI_PROV_EVENT_FAILED,         /*!< 重试失败，已回落配网模式 */
} wifi_prov_event_t;

/**
 * 事件回调
 *
 * 功能：业务层注册回调，接收配网状态变化
 * 修改：2026-10-03 新增
 *
 * @param event 事件类型
 * @param ctx   注册时传入的上下文
 */
typedef void (*wifi_prov_cb_t)(wifi_prov_event_t event, void *ctx);

/**
 * @brief  初始化配网业务层
 *
 * 功能：初始化 NVS 与 wifi_manager；有已保存配置则直接进 STA，否则进 SoftAP 配网。
 *       Captive Portal 由组件内部的 wifi_webserver_start() 自动启用。
 * 修改：2026-10-03 新建
 *
 * @return ESP_OK 成功
 */
esp_err_t wifi_prov_init(void);

/**
 * @brief  设置事件回调
 *
 * 功能：注册配网状态变化的通知回调
 * 修改：2026-10-03 新建
 */
void wifi_prov_set_callback(wifi_prov_cb_t cb, void *ctx);

/**
 * @brief  查询当前配网状态
 *
 * 功能：状态查询
 * 修改：2026-10-03 新建
 */
wifi_prov_state_t wifi_prov_get_state(void);

/**
 * @brief  查询是否已连接网络
 *
 * 功能：状态查询
 * 修改：2026-10-03 新建
 */
bool wifi_prov_is_connected(void);

/**
 * @brief  手动进入配网模式
 *
 * 功能：清除已保存配置并启动 SoftAP + 配网页面，供配网键触发
 * 修改：2026-10-03 新建
 */
esp_err_t wifi_prov_start_portal(void);

/**
 * @brief  手动连接指定 WiFi
 *
 * 功能：保存配置后立即尝试连接，供配网页面的提交动作使用
 * 修改：2026-10-03 新建
 *
 * @param ssid     WiFi 名称
 * @param password WiFi 密码
 */
esp_err_t wifi_prov_connect(const char *ssid, const char *password);

/**
 * @brief  获取设备 IP 地址字符串
 *
 * 功能：配网成功后显示给用户
 * 修改：2026-10-03 新建
 *
 * @param buf  接收缓冲区，建议 >= 16 字节
 * @param len  缓冲区长度
 */
void wifi_prov_get_ip(char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PROV_H */
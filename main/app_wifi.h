/**
 * @file     app_wifi.h
 * @brief    WiFi 配网业务层
 *
 * 功能：封装配网事件回调、状态查询与未来 UI/TTS 接入点，
 *       让 main.c 保持纯装配职责。
 * 修改：2026-10-03 新建（从 main.c 拆出 on_wifi_event）
 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "wifi_prov.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化配网业务层
 *
 * 功能：向 wifi_prov 注册事件回调，后续事件自动转为日志 / 屏幕 / 语音
 * 修改：2026-10-03 新建
 */
void app_wifi_init(void);

/**
 * @brief  查询当前配网状态
 *
 * 功能：状态查询；供 UI / 其它模块轮询
 * 修改：2026-10-03 新建
 */
wifi_prov_state_t app_wifi_get_state(void);

/**
 * @brief  查询是否已连网
 *
 * 功能：状态查询
 * 修改：2026-10-03 新建
 */
bool app_wifi_is_connected(void);

/**
 * @brief  获取连上 WiFi 后的 IP 字符串
 *
 * 功能：供显示屏展示
 * 修改：2026-10-03 新建
 *
 * @param buf  接收缓冲区，建议 >= 16 字节
 * @param len  缓冲区长度
 */
void app_wifi_get_ip(char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_WIFI_H */
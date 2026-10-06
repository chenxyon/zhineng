/**
 * @file     app_wifi.h
 * @brief    WiFi 配网业务层接口
 *
 * 功能：对外暴露 WiFi 连接状态查询，供 UI / 业务层调用
 * 修改：2026-10-06 改用 wifi_manager API（合并原 wifi_prov）
 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 WiFi 管理器（自动选择连接/配网模式）
 */
void app_wifi_init(void);

/**
 * @brief  查询是否已连接外网
 */
bool app_wifi_is_connected(void);

/**
 * @brief  获取连接后的 IP 地址字符串
 *
 * @param buf  接收缓冲区，建议 >= 16 字节
 * @param len  缓冲区长度
 */
void app_wifi_get_ip(char *buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* APP_WIFI_H */

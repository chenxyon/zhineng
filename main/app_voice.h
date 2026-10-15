/**
 * @file     app_voice.h
 * @brief    语音播报业务层接口（本项目私有，有功放才编译）
 *
 * 功能：订阅 app_wifi 的 WiFi 事件，WiFi 连上后用 TTS 播“小智，已启动”，
 *       失败时回落本地旋律；配网模式给两声提示音。
 */
#ifndef APP_VOICE_H
#define APP_VOICE_H

#include "wifi_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册到 app_wifi 的事件回调，由 main.c 在 app_wifi_init() 前调用
 */
void app_voice_on_wifi_event(wifi_mgr_event_t event, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* APP_VOICE_H */

/**
 * @file     board.h
 * @brief    板级装配层
 *
 * 功能：集中存放本板全部引脚定义；负责设备初始化与引脚表打印；
 *       供未来 TTS 等音频业务取回功放参数。
 * 修改：2026-10-03 新建（从 main.c 拆出引脚定义与设备初始化）
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#include "esp_err.h"

#include "oled_spi.h"
#include "max98357.h"
#include "w25q128.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  打印本板全部引脚分配表
 *
 * 功能：把接线打印到日志，便对着原理图 / 实物核对
 * 修改：2026-10-03 从 main.c 拆出
 */
void board_log_pinout(void);

/**
 * @brief  初始化全部外设
 *
 * 功能：依次初始化 OLED、Flash、功放；失败不致命（仅警告），
 *       仅功放失败时会 ERROR_CHECK abort。
 * 修改：2026-10-03 从 main.c 拆出
 */
void board_init_devices(void);

/**
 * @brief  取功放配置（用于后续 TTS）
 *
 * 功能：暴露 audio 参数给其它模块
 * 修改：2026-10-03 新建
 */
const max98357_pins_t *board_audio_config(void);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
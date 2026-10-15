/**
 * @file     app_voice.c
 * @brief    语音播报业务层（本项目私有，有功放才编译）
 *
 * 功能：WiFi 连上 → TTS 在线合成“小智，已启动”，失败/忙时回落小星星旋律；
 *       进配网模式 → 两声短促提示音。
 *
 * @note     事件跑在 WiFi 事件任务里，只能调 tts_hub_speak_async()，
 *           不能调阻塞版 tts_hub_speak()，否则卡死事件循环。
 */
#include "esp_log.h"

#include "max98357.h"
#include "tts_hub.h"
#include "app_voice.h"

static const char *TAG = "VOICE_APP";

/* 小星星旋律（BPM≈120），TTS 不可用时的本地回落 */
static const max98357_note_t s_twinkle[] = {
    { NOTE_C4, BEAT_QUARTER }, { NOTE_C4, BEAT_QUARTER },
    { NOTE_G4, BEAT_QUARTER }, { NOTE_G4, BEAT_QUARTER },
    { NOTE_A4, BEAT_QUARTER }, { NOTE_A4, BEAT_QUARTER },
    { NOTE_G4, BEAT_HALF    },

    { NOTE_F4, BEAT_QUARTER }, { NOTE_F4, BEAT_QUARTER },
    { NOTE_E4, BEAT_QUARTER }, { NOTE_E4, BEAT_QUARTER },
    { NOTE_D4, BEAT_QUARTER }, { NOTE_D4, BEAT_QUARTER },
    { NOTE_C4, BEAT_HALF    },

    { NOTE_G4, BEAT_QUARTER }, { NOTE_G4, BEAT_QUARTER },
    { NOTE_F4, BEAT_QUARTER }, { NOTE_F4, BEAT_QUARTER },
    { NOTE_E4, BEAT_QUARTER }, { NOTE_E4, BEAT_QUARTER },
    { NOTE_D4, BEAT_HALF    },

    { NOTE_G4, BEAT_QUARTER }, { NOTE_G4, BEAT_QUARTER },
    { NOTE_F4, BEAT_QUARTER }, { NOTE_F4, BEAT_QUARTER },
    { NOTE_E4, BEAT_QUARTER }, { NOTE_E4, BEAT_QUARTER },
    { NOTE_D4, BEAT_HALF    },

    { NOTE_C4, BEAT_QUARTER }, { NOTE_C4, BEAT_QUARTER },
    { NOTE_G4, BEAT_QUARTER }, { NOTE_G4, BEAT_QUARTER },
    { NOTE_A4, BEAT_QUARTER }, { NOTE_A4, BEAT_QUARTER },
    { NOTE_G4, BEAT_HALF    },

    { NOTE_F4, BEAT_QUARTER }, { NOTE_F4, BEAT_QUARTER },
    { NOTE_E4, BEAT_QUARTER }, { NOTE_E4, BEAT_QUARTER },
    { NOTE_D4, BEAT_QUARTER }, { NOTE_D4, BEAT_QUARTER },
    { NOTE_C4, BEAT_WHOLE  },

    { 0, 0 }  /* 终止符 */
};

/* 配网模式提示音（两声短促） */
static const max98357_note_t s_portal_beep[] = {
    { NOTE_E4, BEAT_EIGHTH }, { NOTE_G4, BEAT_EIGHTH },
    { 0, 0 }
};

static bool s_played_success_tone = false;

void app_voice_on_wifi_event(wifi_mgr_event_t event, void *ctx)
{
    (void)ctx;
    switch (event) {
    case WIFI_MGR_EV_AP_STARTED:
        max98357_play_melody(s_portal_beep);
        s_played_success_tone = false;
        break;

    case WIFI_MGR_EV_GOT_IP:
        if (!s_played_success_tone) {
            /* 在线 TTS 优先，失败/忙时回落小星星 */
            if (tts_hub_is_busy() ||
                tts_hub_speak_async("小智，已启动") != ESP_OK) {
                ESP_LOGW(TAG, "TTS 不可用，回落本地旋律");
                max98357_play_melody(s_twinkle);
            }
            s_played_success_tone = true;
        }
        break;

    case WIFI_MGR_EV_DISCONNECTED:
        s_played_success_tone = false;
        break;

    default:
        break;
    }
}

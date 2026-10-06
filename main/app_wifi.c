/**
 * @file     app_wifi.c
 * @brief    WiFi 配网业务层
 *
 * 功能：配网事件回调、状态查询、WiFi 连接成功时播放旋律
 * 修改：2026-10-06 合并 wifi_prov，改用 wifi_manager API；
 *          加入「连接成功 → 小星星」音效反馈
 */
#include "esp_log.h"
#include "max98357.h"
#include "wifi_manager.h"

static const char *TAG = "WIFI_APP";

/* 小星星旋律（BPM≈120）*/
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

/* 配网模式提示音（两声短促）*/
static const max98357_note_t s_portal_beep[] = {
    { NOTE_E4, BEAT_EIGHTH }, { NOTE_G4, BEAT_EIGHTH },
    { 0, 0 }
};

static bool s_played_success_tone = false;

static void on_wifi_event(wifi_event_t event, void *ctx)
{
    (void)ctx;
    switch (event) {
    case WIFI_EV_AP_STARTED:
        ESP_LOGW(TAG, "配网模式已启动：连上热点后打开浏览器");
        max98357_play_melody(s_portal_beep);
        s_played_success_tone = false;
        break;

    case WIFI_EV_CONNECTING:
        ESP_LOGI(TAG, "正在连接已保存的 WiFi …");
        break;

    case WIFI_EV_CONNECTED:
        ESP_LOGI(TAG, "已关联热点，等待获取 IP …");
        break;

    case WIFI_EV_GOT_IP: {
        char ip[16] = { 0 };
        wifi_status_t st;
        if (wifi_manager_get_status(&st) == ESP_OK) {
            strncpy(ip, st.ip_address, sizeof(ip) - 1);
        }
        ESP_LOGI(TAG, "WiFi 已连接，IP=%s", ip);
        if (!s_played_success_tone) {
            max98357_play_melody(s_twinkle);
            s_played_success_tone = true;
        }
        break;
    }

    case WIFI_EV_DISCONNECTED:
        ESP_LOGW(TAG, "WiFi 已断开");
        s_played_success_tone = false;
        break;

    case WIFI_EV_FALLBACK:
        ESP_LOGW(TAG, "连接失败，已进入配网模式");
        break;

    default:
        break;
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

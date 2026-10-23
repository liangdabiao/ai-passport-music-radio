// main/mr_page_wifi.c —— Wi-Fi 配网页。
//
// 进入即确认设备处于配网热点模式（esp-wifi-connect 组件全权管理），页面
// 显示手机操作步骤与当前状态。长按确定清除已存凭据并重开热点（两次确认，
// 防误触）。确定短按返回标题页。
#include "mr_app.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "lvgl.h"

#include "mr_sfx.h"
#include "mr_ui.h"
#include "mr_wifi.h"

static const char *TAG = "mr_wifi";

static lv_obj_t *s_status;
static lv_obj_t *s_hint_label;
static lv_timer_t *s_tick;
static bool s_reset_armed;
static mr_topbar_t s_bar;

static void wifi_refresh(void)
{
    if (s_status) {
        char buf[96];
        if (mr_app_wifi_connected()) {
            snprintf(buf, sizeof(buf), "已联网，按确定返回");
        } else {
            snprintf(buf, sizeof(buf),
                     "等待配置：手机连热点 MusicRadio-xxxx，\n"
                     "访问 192.168.4.1 选择家中 Wi-Fi");
        }
        lv_label_set_text(s_status, buf);
    }
    if (s_hint_label) {
        if (s_reset_armed) {
            mr_hint_set(s_hint_label, "再长按一次清除凭据重配");
        } else if (mr_app_wifi_connected()) {
            mr_hint_set(s_hint_label, "已联网 · 长按确定重新配网");
        } else {
            mr_hint_set(s_hint_label, "长按确定清除凭据重配");
        }
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    mr_topbar_set_wifi(&s_bar, mr_app_wifi_connected(), true);
    wifi_refresh();
}

lv_obj_t *mr_page_wifi_enter(void)
{
    s_status = NULL;
    s_hint_label = NULL;
    s_tick = NULL;
    s_reset_armed = false;

    lv_obj_t *card = NULL;
    lv_obj_t *screen = mr_page_create(&card);
    if (!card) return screen;

    s_bar = mr_topbar_create(card, "Wi-Fi 配网");
    mr_topbar_add_battery(&s_bar);
    mr_topbar_set_wifi(&s_bar, mr_app_wifi_connected(), true);

    s_status = mr_label_create(card, "", &mr_font_16, MR_C_INK);
    lv_obj_set_pos(s_status, 20, 60);
    lv_obj_set_width(s_status, MR_PAGE_W - 40);
    lv_obj_set_height(s_status, 90);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(s_status, 6, 0);

    s_hint_label = mr_hint_create(card, "");

    wifi_refresh();
    s_tick = lv_timer_create(tick_cb, 1000, NULL);
    return screen;
}

void mr_page_wifi_leave(void)
{
    if (s_tick) {
        lv_timer_del(s_tick);
        s_tick = NULL;
    }
    s_status = NULL;
    s_hint_label = NULL;
}

void mr_page_wifi_key(const mr_key_t key)
{
    switch (key) {
        case MR_KEY_OK:
            mr_sfx_play(MR_TONE_BACK);
            mr_app_goto_title();
            break;
        case MR_KEY_BACK:
            if (!s_reset_armed) {
                s_reset_armed = true;
                mr_sfx_play(MR_TONE_ENTER);
                wifi_refresh();
                return;
            }
            ESP_LOGI(TAG, "清除 Wi-Fi 凭据并重开配网热点");
            mr_wifi_reset_credentials();
            s_reset_armed = false;
            mr_sfx_play(MR_TONE_BACK);
            wifi_refresh();
            break;
        default:
            break;
    }
}

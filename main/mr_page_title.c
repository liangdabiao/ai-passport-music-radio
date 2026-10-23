// main/mr_page_title.c —— 标题页：开机第一屏。
//
// 复古收音机的「铭牌」：大字台标 + 一排装饰刻度 + 联网状态。
//   确定 短按 = 开始收听（进收音机页，不自动开播）
//   确定 长按 = Wi-Fi 配网页
#include "mr_app.h"

#include <stdio.h>

#include "lvgl.h"

#include "mr_sfx.h"
#include "mr_station.h"
#include "mr_ui.h"

static lv_timer_t *s_tick;
static lv_obj_t *s_wifi_label;
static mr_topbar_t s_bar;

static void wifi_refresh(void)
{
    if (!s_wifi_label) return;
    if (mr_app_wifi_connected()) {
        lv_label_set_text(s_wifi_label, "已联网");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(MR_C_OK), 0);
    } else {
        lv_label_set_text(s_wifi_label, "未联网 · 长按确定配网");
        lv_obj_set_style_text_color(s_wifi_label, lv_color_hex(MR_C_MUTED), 0);
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    wifi_refresh();
    mr_topbar_set_wifi(&s_bar, mr_app_wifi_connected(), true);
}

lv_obj_t *mr_page_title_enter(void)
{
    s_wifi_label = NULL;
    s_tick = NULL;

    lv_obj_t *card = NULL;
    lv_obj_t *screen = mr_page_create(&card);
    if (!card) return screen;

    s_bar = mr_topbar_create(card, "");
    mr_topbar_add_battery(&s_bar);
    mr_topbar_set_wifi(&s_bar, mr_app_wifi_connected(), true);

    // 铭牌区。
    lv_obj_t *title = mr_label_create(card, "音乐电台", &mr_font_32, MR_C_AMBER);
    lv_obj_set_width(title, MR_PAGE_W - 20);
    lv_obj_set_height(title, 42);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 10, 78);

    lv_obj_t *sub = mr_label_create(card, "", &mr_font_16, MR_C_MUTED);
    char sub_text[56];
    snprintf(sub_text, sizeof(sub_text), "%u 个音乐电台 · 流行经典",
             (unsigned)MR_STATION_COUNT);
    lv_label_set_text(sub, sub_text);
    lv_obj_set_width(sub, MR_PAGE_W - 20);
    lv_obj_set_height(sub, MR_LINE_H_SMALL);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(sub, 10, 128);

    // 装饰刻度带（迷你刻度盘：只有主刻度与指针，静止）。
    lv_obj_t *band = lv_obj_create(card);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(band, 150, 26);
    lv_obj_set_pos(band, (MR_PAGE_W - 150) / 2, 170);
    lv_obj_set_style_radius(band, 8, 0);
    lv_obj_set_style_bg_color(band, lv_color_hex(MR_C_SCREEN), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(band, 2, 0);
    lv_obj_set_style_border_color(band, lv_color_hex(MR_C_LINE), 0);
    lv_obj_set_style_pad_all(band, 0, 0);
    for (int f = MR_DIAL_FREQ_LO; f <= MR_DIAL_FREQ_HI; f += 4) {
        const int x = 10 + ((f - MR_DIAL_FREQ_LO) * 130) /
                               (MR_DIAL_FREQ_HI - MR_DIAL_FREQ_LO);
        lv_obj_t *tick = lv_obj_create(band);
        lv_obj_remove_flag(tick, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(tick, 2, 10);
        lv_obj_set_pos(tick, x, 8);
        lv_obj_set_style_radius(tick, 0, 0);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_set_style_pad_all(tick, 0, 0);
        lv_obj_set_style_bg_color(tick, lv_color_hex(MR_C_AMBER_D), 0);
        lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    }
    lv_obj_t *deco = lv_obj_create(band);
    lv_obj_remove_flag(deco, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(deco, 3, 18);
    lv_obj_set_pos(deco, 72, 4);
    lv_obj_set_style_radius(deco, 1, 0);
    lv_obj_set_style_border_width(deco, 0, 0);
    lv_obj_set_style_pad_all(deco, 0, 0);
    lv_obj_set_style_bg_color(deco, lv_color_hex(MR_C_POINTER), 0);
    lv_obj_set_style_bg_opa(deco, LV_OPA_COVER, 0);

    s_wifi_label = mr_label_create(card, "", &mr_font_16, MR_C_MUTED);
    lv_obj_set_width(s_wifi_label, MR_PAGE_W - 20);
    lv_obj_set_height(s_wifi_label, MR_LINE_H_SMALL);
    lv_obj_set_style_text_align(s_wifi_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_wifi_label, 10, 216);

    (void)mr_hint_create(card, "OK 开始收听 · 长按配网");

    wifi_refresh();
    s_tick = lv_timer_create(tick_cb, 500, NULL);
    return screen;
}

void mr_page_title_leave(void)
{
    if (s_tick) {
        lv_timer_del(s_tick);
        s_tick = NULL;
    }
    s_wifi_label = NULL;
}

void mr_page_title_key(const mr_key_t key)
{
    switch (key) {
        case MR_KEY_OK:
            mr_sfx_play(MR_TONE_ENTER);
            mr_app_goto_radio();
            break;
        case MR_KEY_BACK: // 标题页没有「返回」：长按确定 = 配网
            mr_sfx_play(MR_TONE_ENTER);
            mr_app_goto_wifi();
            break;
        default:
            break;
    }
}

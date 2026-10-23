// main/mr_app.h —— 导航、应用状态与页面跳转。
//
// 硬件只有三个键（上/下/确定）。家族惯例 CLICK（松手判定）+ LONG：
//   上 / 下 短按  换台（收音机页，线性循环）/ 无（标题页）
//   确定 短按   标题=收听；收音机页=播放/暂停；配网页=返回
//   确定 长按   收音机页=返回标题（并停播）；标题=进配网页；配网页=重新配网
//   上 / 下 长按 收音机页=音量 ±（浮层显示，自动消隐）
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum {
    MR_KEY_UP = 0,
    MR_KEY_DOWN,
    MR_KEY_OK,
    MR_KEY_BACK,    // 长按确定：各页自定义（返回 / 配网）
    MR_KEY_VOL_UP,  // 长按上：音量 +
    MR_KEY_VOL_DOWN, // 长按下：音量 -
} mr_key_t;

void mr_app_start(void);
void mr_app_key(mr_key_t key);

// ---- 页面跳转 ----
void mr_app_goto_title(void);
void mr_app_goto_radio(void);
void mr_app_goto_wifi(void);

// ---- 共享状态 ----
uint8_t mr_app_station(void);
void mr_app_set_station(uint8_t index); // 只改选择，不碰播放器

uint8_t mr_app_volume(void);
// 音量 ± 一档：夹边、落盘、应用到播放器、通知收音机页浮层。dir = ±1。
void mr_app_volume_step(int dir);

bool mr_app_wifi_connected(void);
void mr_app_note_wifi(bool connected);

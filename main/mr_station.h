// main/mr_station.h —— 内置电台清单与分类（蜻蜓 FM 音乐台精选）。
//
// 清单固化在固件里（v1 不做在线搜台）。每个地址都是 2026-10-23 从
// www.qtfm.cn 音乐分类页取得并逐台实测（纯 http GET 收流 ≥16KB）——
// 设备端 MP3 直播流管线与 net-radio 同源（两代真机验证）。
//
// 本模块与 ESP-IDF/LVGL 解耦，宿主测试直接编译它。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MR_CAT_POP,
    MR_CAT_CLASSIC,
    MR_CAT_ASIA,
    MR_CAT_LOCAL,
    MR_CAT_COUNT,
} mr_cat_t;

typedef struct {
    const char *name; // 短名：全角 ≤7 字或半角 ≤13 字符（24px 一行硬保证）
    const char *url;  // 蜻蜓直播流（纯 http MP3）
    uint8_t cat;      // mr_cat_t
} mr_station_t;

#define MR_STATION_COUNT 18
extern const mr_station_t MR_STATIONS[MR_STATION_COUNT];

uint8_t mr_station_next(uint8_t index);
uint8_t mr_station_prev(uint8_t index);
const char *mr_station_cat_name(uint8_t cat);
bool mr_station_valid(uint8_t index);

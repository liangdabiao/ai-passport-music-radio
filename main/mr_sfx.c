// main/mr_sfx.c —— 见 mr_sfx.h。
#include "mr_sfx.h"

#include <stddef.h>

#include "bsp_audio.h"
#include "esp_err.h"

#include "mr_player.h"
#include "mr_volume.h"
#include "rtttl_player.h"

#define RTTTL_HZ 16000 // 与 rtttl_player.c 的 RTTTL_SAMPLE_RATE 一致

// 音效在用户音量 100% 时的 codec 音量基准；必须与 mr_volume.h 的
// MR_VOLUME_BASE_SFX 一致 —— 由 tests/test_mr_volume.c 钉住。
#define MR_SFX_BASE_VOLUME 58

// 音效旋律（RTTTL）。少而轻：移动嗒、开始叮、返回噔、拒绝噔噔。
static const char *const TONES[MR_TONE_COUNT] = {
    [MR_TONE_MOVE]     = "mv:d=4,o=5,b=500:c7",
    [MR_TONE_ENTER]    = "en:d=8,o=5,b=300:e",
    [MR_TONE_BACK]     = "bk:d=8,o=5,b=300:g,e",
    [MR_TONE_DECLINED] = "no:d=4,o=4,b=500:c",
};

static uint8_t s_level = MR_VOLUME_DEFAULT;

esp_err_t mr_sfx_start(void)
{
    return rtttl_player_start();
}

void mr_sfx_play(const mr_tone_t tone)
{
    if (tone >= MR_TONE_COUNT) return;
    if (s_level == MR_VOLUME_OFF) return;
    if (mr_player_is_streaming()) return; // 电台正在出声：不打断节目

    // 播放器可能把 codec 留在流采样率上；无条件恢复 16kHz（同格式重复调用
    // 是廉价的，见 bsp_audio.h）。
    (void)bsp_audio_set_format(RTTTL_HZ, 16, 1);
    bsp_audio_set_volume(mr_volume_scale(MR_SFX_BASE_VOLUME, s_level));
    (void)rtttl_player_play(TONES[tone]);
}

void mr_sfx_set_volume(const uint8_t level)
{
    s_level = level;
}

void mr_sfx_stop(void)
{
    rtttl_player_stop();
}

// main/mr_volume.c —— 见 mr_volume.h。
#include "mr_volume.h"

#include <stdio.h>
#include <string.h>

_Static_assert(MR_VOLUME_MAX % MR_VOLUME_STEP == 0, "满量程必须是档距的整数倍，否则到顶会跳出格点");
_Static_assert(MR_VOLUME_DEFAULT % MR_VOLUME_STEP == 0, "默认档位必须落在档距格点上");

uint8_t mr_volume_up(const uint8_t level)
{
    if (level > MR_VOLUME_MAX) return MR_VOLUME_STEP; // 越界输入：从第一格重新起步
    const uint32_t next = (uint32_t)level + MR_VOLUME_STEP;
    return (uint8_t)(next > MR_VOLUME_MAX ? MR_VOLUME_MAX : next);
}

uint8_t mr_volume_down(const uint8_t level)
{
    if (level > MR_VOLUME_MAX) return MR_VOLUME_OFF;
    return (uint8_t)(level > MR_VOLUME_STEP ? level - MR_VOLUME_STEP : MR_VOLUME_OFF);
}

uint8_t mr_volume_scale(const uint8_t base, const uint8_t level)
{
    // 先转 32 位再乘：level 越界到 255 时 8 位乘法会溢出。
    // 截断而不是四舍五入：与家族各应用同一套算术，行为可复现。
    return (uint8_t)(((uint32_t)base * (uint32_t)level) / 100u);
}

uint16_t mr_volume_text(const uint8_t level, char *out, const size_t capacity)
{
    if (!out || capacity == 0) return 0;
    out[0] = '\0';

    if (level == MR_VOLUME_OFF) {
        // 「关」是一个汉字，UTF-8 占 3 字节。容量不足时整体失败，不留半截。
        static const char k_off[] = "关";
        if (sizeof(k_off) > capacity) return 0;
        memcpy(out, k_off, sizeof(k_off));
        return (uint16_t)(sizeof(k_off) - 1u);
    }

    char buffer[8];
    const int written = snprintf(buffer, sizeof(buffer), "%u%%", (unsigned)level);
    if (written <= 0 || (size_t)written + 1u > capacity) return 0;
    memcpy(out, buffer, (size_t)written + 1u);
    return (uint16_t)written;
}

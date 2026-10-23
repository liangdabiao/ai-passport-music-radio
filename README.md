<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Music Radio — pop & classics from Qingting FM on a three-key handheld

A music-only internet radio for the FoloToy AI Passport, sourced from
**[Qingting FM](https://www.qtfm.cn/)** (蜻蜓 FM), one of China's largest radio
platforms: eighteen curated all-music stations — Shanghai's Dynamic 101 and
Love Radio, the AsiaFM family, classic hits, and provincial music broadcasters
— grouped in four cycles (Pop / Classics / Asia / Local). Every stream was
verified alive (plain-HTTP GET, real bytes received) before being baked into
the firmware.

Tap Up/Down to tune, OK to play or pause, hold Up/Down for volume. A retro
dial with a sliding needle — in a neon-indigo theme, the third skin of the
family's radio trio (amber "Net Radio", China-red "CNR Radio", and this one).

| Title | Tuning screen | Volume overlay |
| --- | --- | --- |
| Nameplate, network state, decorative dial. | Dial with a needle per station, station name, live status and bitrate, dancing tuning bars while playing. | Hold Up/Down; auto-hides after 1.6 s. |

## Stations

| Group | Stations |
| --- | --- |
| Pop 流行 | Dynamic 101 动感101, Love Radio, 动听音乐台, 星河音乐, 清晨音乐台 |
| Classics 经典 | 怀旧好声音, 江苏经典流行音乐, 两广之声音乐台 |
| Asia 亚洲 | AsiaFM HD, 亚洲天空台, 亚洲粤语台, 亚洲经典台 |
| Local 省市 | 广东音乐之声, 顺德音乐之声, 怀集音乐之声, 苏州音乐广播, 河北音乐广播, 哈尔滨音乐广播 |

Streams are plain-HTTP MP3 (`lhttp.qtfm.cn/live/{id}/64k.mp3`) — the same
two-generations-proven pipeline as the sibling apps, so no TLS is needed on
this PSRAM-less device. Station list lives in `main/mr_station.c`; regenerate
the CJK font subsets with `tools/music-radio/gen_font.py` after editing names.

## Getting started

1. First run: title screen → hold **OK** → join the device hotspot
   **MusicRadio** → open `192.168.4.1` → enter your home Wi-Fi (2.4 GHz).
   The device joins and remembers it.
2. **OK** opens the tuning screen; **OK** again plays. Tap Up/Down to tune
   across the four groups; hold Up/Down for volume (0–100, ten steps).
3. Press OK during playback to pause; hold OK to stop and return to the title
   screen. The last station and volume survive reboots.

## Build

An [ESP-IDF](https://github.com/espressif/esp-idf) 5.5.3 project for the FoloToy
AI Passport (ESP32-C3, 240×320 ST7789, three-key ADC input, ES8311 codec).

```bash
idf.py build
idf.py flash monitor
./tools/validate.sh
```

## Licence and credits

Station data and streams belong to Qingting FM (蜻蜓 FM); this project is for
personal study. A FoloToy AI Passport family app — sibling of Net Radio (amber)
and CNR Radio (China-red), and of twelve more. The MP3 streaming pipeline is
the family-proven one (raw-socket HTTP → ring buffer → Helix fixed-point MP3 →
mono downmix → ES8311); Wi-Fi provisioning comes from the family-verified
esp-wifi-connect component; the board support package and repository tools are
the family-shared platform layer (MIT, see [`LICENSE`](LICENSE)).

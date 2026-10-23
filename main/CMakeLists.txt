idf_component_register(
    SRCS "main.c"
         "fap_screenshot.c"
         # 应用与页面
         "mr_app.c"
         "mr_page_title.c"
         "mr_page_tuner.c"
         "mr_page_wifi.c"
         "mr_ui.c"
         # 纯逻辑：与 ESP-IDF/LVGL 解耦，宿主测试直接编译这些文件
         "mr_station.c"
         "mr_volume.c"
         "mr_http.c"
         # 流媒体播放（raw-socket HTTP → 环形缓冲 → libhelix MP3 → PCM）
         "mr_player.c"
         # 服务与网络
         "mr_store.c"
         "mr_sfx.c"
         "mr_wifi.cc"
         # 音效合成（RTTTL）。
         "rtttl_player.c"
    INCLUDE_DIRS "."
    # 联网应用需要 WiFi/网络栈、esp-wifi-connect 配网组件与 MP3 解码器；
    # BLE 不初始化。
    REQUIRES bsp esp_event esp_hw_support esp_timer nvs_flash esp_wifi
             esp_netif lwip esp-wifi-connect libhelix_mp3
)

# 中文字库由 tools/music-radio/gen_font.py 生成到 assets/fonts/。字符集 =
# 界面文案 + 全部台名的并集（tools/music-radio/charset.txt 钉住清单）。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/mr_font_16.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/mr_font_24.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/mr_font_32.c"
)

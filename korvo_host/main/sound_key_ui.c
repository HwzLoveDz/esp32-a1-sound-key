/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
/* Live sound-key interface. Device telemetry is the only unlock authority. */
#include "sound_key_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sound_key_link.h"
#include "sound_key_fonts.h"
#include "dev_display_lcd.h"
#include "dev_lcd_touch.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#define SCREEN_W 800
#define SCREEN_H 480
#define C_BG 0x08131E
#define C_PANEL 0x102130
#define C_SOFT 0x193242
#define C_LINE 0x284253
#define C_INK 0xEDF6FC
#define C_MUTED 0x9AB3C3
#define C_ACCENT 0x53E5BF
#define C_WARN 0xFFD08B
#define C_ERROR 0xFF9698
#define FATAL_FAULTS (SK_FAULT_BOARD | SK_FAULT_ADC | SK_FAULT_I2C)

static const char *TAG = "sound_key_ui";
static const lv_point_precise_t s_closed_shackle[] = {
    {0, 29}, {0, 17}, {2, 9}, {7, 3}, {15, 0},
    {23, 1}, {30, 5}, {34, 13}, {34, 29},
};
static const lv_point_precise_t s_open_shackle[] = {
    {0, 29}, {0, 17}, {2, 9}, {7, 3}, {15, 0},
    {23, 1}, {30, 5}, {34, 13},
};

typedef struct {
    lv_obj_t *backend, *backend_hint, *status, *title, *subtitle;
    lv_obj_t *frequency, *strength, *hold, *input_note, *count, *footer_hint;
    lv_obj_t *ring, *body, *shackle, *key_dot, *key_slot;
    lv_obj_t *strength_bar, *hold_bar, *target_button, *relock_button;
    lv_obj_t *diagnostic, *diag_values, *diag_hint;
    uint8_t requested_command;
    uint32_t command_error_until_ms;
} ui_state_t;

static ui_state_t s_ui;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width,
                            const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    if (width > 0) lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_shape(lv_obj_t *parent, int x, int y, int width,
                            int height, int radius, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int width,
                             const char *text, bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 46);
    lv_obj_set_style_radius(button, 9, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(primary ? C_ACCENT : C_SOFT), 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *label = make_label(button, 0, 0, 0, &sound_key_font_16,
                                  primary ? C_BG : C_INK, text);
    lv_obj_center(label);
    return button;
}

static void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static void set_lock(bool unlocked, uint32_t color)
{
    lv_obj_set_style_bg_color(s_ui.body, lv_color_hex(color), 0);
    lv_obj_set_style_line_color(s_ui.shackle, lv_color_hex(color), 0);
    lv_line_set_points(s_ui.shackle, unlocked ? s_open_shackle : s_closed_shackle,
                        unlocked ? 8 : 9);
    lv_obj_set_pos(s_ui.shackle, unlocked ? 188 : 183, 145);
}

static bool data_is_fresh(const sound_key_snapshot_t *snap)
{
    /* The link intentionally clears valid during its acknowledgement barrier.
     * Recent pending telemetry may explain "setting", never an unlock. */
    return snap->connected && (snap->valid || snap->command_pending) &&
           snap->last_packet_ms != 0U &&
           (uint32_t)(now_ms() - snap->last_packet_ms) <= SK_LINK_STALL_MS;
}

static const char *backend_name(uint8_t backend)
{
    switch (backend) {
    case SK_BACKEND_HW: return "A1 硬件";
    case SK_BACKEND_SOFTWARE: return "软件参考";
    case SK_BACKEND_UNCONFIRMED: return "硬件待确认";
    default: return "后端未识别";
    }
}

static void render_snapshot(const sound_key_snapshot_t *snap)
{
    const bool fresh = data_is_fresh(snap);
    const bool fatal = fresh && ((snap->fault_flags & FATAL_FAULTS) != 0 ||
                                snap->state == SK_FAULT);
    const bool confirmed_backend = snap->backend == SK_BACKEND_HW ||
                                   snap->backend == SK_BACKEND_SOFTWARE;
    const bool pending = fresh && snap->command_pending;
    const bool can_control = fresh && !fatal && !pending;
    uint32_t color = C_MUTED;
    const char *title = "请连接 A1";
    const char *subtitle = "Korvo USB-A 连接 A1 USB DEVICE";
    const char *status = "等待连接";
    bool unlocked = false;
    unsigned progress = 0;

    if (!fresh) {
        s_ui.requested_command = 0;
        if (snap->connected) {
            title = snap->valid ? "数据已暂停" : "等待设备数据";
            subtitle = "连接恢复后重新确认解锁状态";
            status = "等待数据";
        }
    } else if (fatal) {
        title = "检测暂不可用";
        subtitle = (snap->fault_flags & SK_FAULT_BOARD) ? "A1 板级初始化失败" :
                   (snap->fault_flags & SK_FAULT_ADC) ? "麦克风采集异常" :
                   (snap->fault_flags & SK_FAULT_I2C) ? "A1 通信异常" : "请查看 A1 日志";
        status = "设备故障";
        color = C_ERROR;
    } else if (pending) {
        title = s_ui.requested_command == SK_CMD_RELOCK ? "正在上锁" : "设置中";
        subtitle = "等待 A1 确认";
        status = "命令待确认";
        color = C_WARN;
    } else if (!confirmed_backend) {
        title = "硬件待确认";
        subtitle = "标定完成后启用硬件解锁";
        status = "结果待验证";
        color = C_WARN;
        s_ui.requested_command = 0;
    } else {
        s_ui.requested_command = 0;
        switch (snap->state) {
        case SK_UNLOCKED:
            title = "已解锁";
            subtitle = "声音钥匙验证通过";
            status = "解锁成功";
            color = C_ACCENT;
            unlocked = true;
            progress = 100;
            break;
        case SK_MATCHING:
            title = "正在匹配";
            subtitle = "保持音调，马上解锁";
            status = "音调命中";
            color = C_ACCENT;
            progress = snap->hold_ms ? (unsigned)snap->held_ms * 100U / snap->hold_ms : 0;
            break;
        case SK_WRONG:
            title = "音调不匹配";
            subtitle = "有声音，但目标音调强度不足";
            status = "未命中";
            color = C_WARN;
            break;
        case SK_WAITING:
            title = "等待声音钥匙";
            subtitle = NULL;
            status = "正在聆听";
            break;
        default:
            title = "结果待验证";
            subtitle = "设备状态未识别";
            status = "状态未识别";
            color = C_WARN;
            break;
        }
    }
    if (progress > 100) progress = 100;
    lv_label_set_text(s_ui.title, title);
    lv_obj_set_style_text_color(s_ui.title, lv_color_hex(color == C_MUTED ? C_INK : color), 0);
    lv_label_set_text(s_ui.status, status);
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(color), 0);
    if (subtitle) lv_label_set_text(s_ui.subtitle, subtitle);
    else lv_label_set_text_fmt(s_ui.subtitle, "播放指定音调，保持 %u.%02u 秒",
                               (unsigned)snap->hold_ms / 1000U,
                               ((unsigned)snap->hold_ms % 1000U) / 10U);
    set_lock(unlocked, color);
    lv_obj_set_style_arc_color(s_ui.ring, lv_color_hex(color), LV_PART_INDICATOR);
    lv_arc_set_value(s_ui.ring, (int)progress);
    lv_bar_set_value(s_ui.hold_bar, (int)progress, LV_ANIM_OFF);

    lv_label_set_text(s_ui.backend, fresh ? backend_name(snap->backend) : "未连接");
    lv_label_set_text(s_ui.backend_hint, !fresh ? "等待设备数据" :
                       snap->backend == SK_BACKEND_SOFTWARE ? "硬件待标定" :
                       snap->backend == SK_BACKEND_HW ? "硬件判定" : "结果待验证");
    lv_obj_set_style_text_color(s_ui.backend, lv_color_hex(fresh && snap->backend == SK_BACKEND_HW ?
                                                          C_ACCENT : C_INK), 0);
    set_enabled(s_ui.target_button, can_control);
    set_enabled(s_ui.relock_button, can_control);

    if (fresh) {
        const unsigned strength = snap->strength_permille > 1000U ? 1000U : snap->strength_permille;
        lv_label_set_text_fmt(s_ui.frequency, "%lu Hz", (unsigned long)snap->target_hz);
        lv_label_set_text_fmt(s_ui.strength, "%u.%u%%", strength / 10U, strength % 10U);
        lv_bar_set_value(s_ui.strength_bar, (int)strength, LV_ANIM_OFF);
        /* A pending command never presents the preceding unlock as confirmation. */
        const unsigned held = pending || fatal || !confirmed_backend ? 0U : snap->held_ms;
        lv_label_set_text_fmt(s_ui.hold, "%u.%02u / %u.%02u s", held / 1000U,
                               (held % 1000U) / 10U, (unsigned)snap->hold_ms / 1000U,
                               ((unsigned)snap->hold_ms % 1000U) / 10U);
        lv_label_set_text_fmt(s_ui.count, "%s · 已解锁 %02lu 次", backend_name(snap->backend),
                               (unsigned long)snap->unlock_count);
        lv_label_set_text_fmt(s_ui.input_note, "输入 RMS: %u%s", (unsigned)snap->rms,
                               (snap->fault_flags & SK_FAULT_INPUT_CLIPPED) ? " · 音量过大" : "");
        lv_label_set_text(s_ui.footer_hint,
                           snap->backend == SK_BACKEND_SOFTWARE ? "解锁判定来自软件参考" :
                           snap->backend == SK_BACKEND_HW ? "解锁判定来自 A1 硬件" : "硬件结果待标定确认");
    } else {
        lv_label_set_text(s_ui.frequency, "-- Hz");
        lv_label_set_text(s_ui.strength, "--");
        lv_label_set_text(s_ui.hold, "-- / -- s");
        lv_label_set_text(s_ui.count, "解锁状态待确认");
        lv_label_set_text(s_ui.input_note, "等待新检测结果");
        lv_label_set_text(s_ui.footer_hint, "连接后可切换音调和重新上锁");
        lv_bar_set_value(s_ui.strength_bar, 0, LV_ANIM_OFF);
    }
    if (s_ui.command_error_until_ms != 0 &&
        (int32_t)(s_ui.command_error_until_ms - now_ms()) > 0) {
        lv_label_set_text(s_ui.footer_hint, "命令未发送，请重试");
    } else {
        s_ui.command_error_until_ms = 0;
    }
    if (s_ui.diagnostic && !lv_obj_has_flag(s_ui.diagnostic, LV_OBJ_FLAG_HIDDEN)) {
        if (fresh && (snap->fault_flags & SK_FAULT_HW_READ_UNAVAILABLE) != 0) {
            lv_label_set_text(s_ui.diag_values, "硬件读数尚不可用");
        } else if (fresh) {
            lv_label_set_text_fmt(s_ui.diag_values,
                                   "单频 raw: %lu\n总能量 raw: %lu\n系数 raw: %u",
                                   (unsigned long)snap->tone_power, (unsigned long)snap->total_power,
                                   (unsigned)snap->coefficient);
        } else {
            lv_label_set_text(s_ui.diag_values, "等待 A1 最新读数");
        }
        lv_label_set_text(s_ui.diag_hint, "raw 不代表百分比，硬件系数待实机标定");
    }
}

static void command_event(lv_event_t *event)
{
    sound_key_snapshot_t snap;
    sound_key_link_get_snapshot(&snap);
    if (!data_is_fresh(&snap) || snap.command_pending ||
        (snap.fault_flags & FATAL_FAULTS) != 0 || snap.state == SK_FAULT) return;
    const bool change_target = lv_event_get_target_obj(event) == s_ui.target_button;
    const uint32_t next_hz = snap.target_hz == 500U ? 1000U :
                             snap.target_hz == 1000U ? 2000U : 500U;
    const esp_err_t result = change_target ? sound_key_link_set_target(next_hz) : sound_key_link_relock();
    if (result == ESP_OK) {
        s_ui.requested_command = change_target ? SK_CMD_SET_TARGET : SK_CMD_RELOCK;
    } else {
        s_ui.command_error_until_ms = now_ms() + 3000U;
        ESP_LOGW(TAG, "UI command: %s", esp_err_to_name(result));
    }
    sound_key_link_get_snapshot(&snap);
    render_snapshot(&snap);
}

static void diagnostic_open(lv_event_t *event)
{
    (void)event;
    lv_obj_remove_flag(s_ui.diagnostic, LV_OBJ_FLAG_HIDDEN);
    sound_key_snapshot_t snap;
    sound_key_link_get_snapshot(&snap);
    render_snapshot(&snap);
}

static void diagnostic_close(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(s_ui.diagnostic, LV_OBJ_FLAG_HIDDEN);
}

static void ui_timer(lv_timer_t *timer)
{
    (void)timer;
    sound_key_snapshot_t snap;
    sound_key_link_get_snapshot(&snap);
    render_snapshot(&snap);
}

static void create_interface(lv_obj_t *root)
{
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
    const int heights[] = {12, 25, 36, 25, 12};
    for (int i = 0; i < 5; ++i)
        make_shape(root, 25 + i * 6, 27 - heights[i] / 2, 3, heights[i], 2, C_ACCENT);
    make_label(root, 66, 6, 270, &sound_key_font_24, C_INK, "声音钥匙");
    make_label(root, 67, 39, 290, &sound_key_font_16, C_MUTED, "ESP32-A1 · SOUND KEY");
    s_ui.backend_hint = make_label(root, 475, 21, 159, &sound_key_font_16, C_MUTED, "等待设备数据");
    lv_obj_set_style_text_align(s_ui.backend_hint, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *pill = make_shape(root, 647, 15, 128, 34, 17, C_SOFT);
    s_ui.backend = make_label(pill, 0, 4, 128, &sound_key_font_16, C_INK, "未连接");
    lv_obj_set_style_text_align(s_ui.backend, LV_TEXT_ALIGN_CENTER, 0);
    make_shape(root, 0, 72, SCREEN_W, 1, 0, C_LINE);

    s_ui.ring = lv_arc_create(root);
    lv_obj_set_pos(s_ui.ring, 110, 93);
    lv_obj_set_size(s_ui.ring, 180, 180);
    lv_arc_set_bg_angles(s_ui.ring, 0, 360);
    lv_arc_set_rotation(s_ui.ring, 270);
    lv_arc_set_range(s_ui.ring, 0, 100);
    lv_arc_set_value(s_ui.ring, 0);
    lv_obj_set_style_arc_width(s_ui.ring, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui.ring, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ui.ring, lv_color_hex(C_SOFT), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ui.ring, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_ui.ring, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_border_width(s_ui.ring, 0, LV_PART_KNOB);
    lv_obj_remove_flag(s_ui.ring, LV_OBJ_FLAG_CLICKABLE);
    s_ui.body = make_shape(root, 170, 173, 61, 48, 10, C_MUTED);
    s_ui.shackle = lv_line_create(root);
    lv_obj_set_style_line_width(s_ui.shackle, 6, 0);
    lv_obj_set_style_line_rounded(s_ui.shackle, true, 0);
    lv_obj_set_style_line_color(s_ui.shackle, lv_color_hex(C_MUTED), 0);
    s_ui.key_dot = make_shape(root, 196, 186, 10, 10, 5, C_BG);
    s_ui.key_slot = make_shape(root, 199, 193, 4, 13, 2, C_BG);
    set_lock(false, C_MUTED);
    s_ui.status = make_label(root, 36, 279, 328, &sound_key_font_16, C_MUTED, "等待连接");
    lv_obj_set_style_text_align(s_ui.status, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.title = make_label(root, 26, 307, 348, &sound_key_font_32, C_INK, "请连接 A1");
    lv_obj_set_style_text_align(s_ui.title, LV_TEXT_ALIGN_CENTER, 0);
    s_ui.subtitle = make_label(root, 29, 358, 342, &sound_key_font_16, C_MUTED,
                                "Korvo USB-A 连接 A1 USB DEVICE");
    lv_obj_set_style_text_align(s_ui.subtitle, LV_TEXT_ALIGN_CENTER, 0);

    make_label(root, 419, 95, 350, &sound_key_font_16, C_MUTED, "钥匙音调");
    s_ui.frequency = make_label(root, 416, 116, 358, &sound_key_font_48, C_INK, "-- Hz");
    lv_obj_add_flag(s_ui.frequency, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.frequency, diagnostic_open, LV_EVENT_LONG_PRESSED, NULL);
    make_shape(root, 419, 194, 352, 1, 0, C_LINE);
    make_label(root, 419, 215, 220, &sound_key_font_16, C_MUTED, "目标音调强度");
    s_ui.strength = make_label(root, 668, 206, 103, &sound_key_font_24, C_INK, "--");
    lv_obj_set_style_text_align(s_ui.strength, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.strength_bar = lv_bar_create(root);
    lv_obj_set_pos(s_ui.strength_bar, 419, 248);
    lv_obj_set_size(s_ui.strength_bar, 352, 10);
    lv_bar_set_range(s_ui.strength_bar, 0, 1000);
    lv_obj_set_style_bg_color(s_ui.strength_bar, lv_color_hex(C_SOFT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.strength_bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    make_label(root, 419, 266, 80, &sound_key_font_16, C_MUTED, "弱");
    lv_obj_t *strong = make_label(root, 711, 266, 60, &sound_key_font_16, C_MUTED, "强");
    lv_obj_set_style_text_align(strong, LV_TEXT_ALIGN_RIGHT, 0);
    make_label(root, 419, 310, 170, &sound_key_font_16, C_MUTED, "连续命中");
    s_ui.hold = make_label(root, 588, 310, 183, &sound_key_font_16, C_MUTED, "-- / -- s");
    lv_obj_set_style_text_align(s_ui.hold, LV_TEXT_ALIGN_RIGHT, 0);
    s_ui.hold_bar = lv_bar_create(root);
    lv_obj_set_pos(s_ui.hold_bar, 419, 341);
    lv_obj_set_size(s_ui.hold_bar, 352, 5);
    lv_bar_set_range(s_ui.hold_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ui.hold_bar, lv_color_hex(C_SOFT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.hold_bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    s_ui.input_note = make_label(root, 419, 363, 352, &sound_key_font_16, C_MUTED, "等待新检测结果");

    make_shape(root, 0, 401, SCREEN_W, 1, 0, C_LINE);
    s_ui.count = make_label(root, 25, 414, 462, &sound_key_font_16, C_INK, "解锁状态待确认");
    s_ui.footer_hint = make_label(root, 25, 445, 462, &sound_key_font_16, C_MUTED,
                                   "连接后可切换音调和重新上锁");
    s_ui.target_button = make_button(root, 501, 420, 126, "切换音调", false);
    s_ui.relock_button = make_button(root, 638, 420, 137, "重新上锁", true);
    lv_obj_add_event_cb(s_ui.target_button, command_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ui.relock_button, command_event, LV_EVENT_CLICKED, NULL);

    s_ui.diagnostic = make_shape(root, 0, 0, SCREEN_W, SCREEN_H, 0, 0x000000);
    lv_obj_set_style_bg_opa(s_ui.diagnostic, LV_OPA_60, 0);
    lv_obj_add_flag(s_ui.diagnostic, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *card = make_shape(s_ui.diagnostic, 94, 116, 612, 248, 15, C_PANEL);
    make_label(card, 25, 13, 435, &sound_key_font_24, C_INK, "硬件诊断");
    s_ui.diag_values = make_label(card, 25, 59, 435, &sound_key_font_24, C_MUTED,
                                   "等待 A1 最新读数");
    lv_obj_set_style_text_line_space(s_ui.diag_values, 4, 0);
    s_ui.diag_hint = make_label(card, 25, 207, 570, &sound_key_font_16, C_MUTED,
                                 "raw 不代表百分比，硬件系数待实机标定");
    lv_obj_t *close = make_button(card, 444, 154, 139, "关闭", false);
    lv_obj_add_event_cb(close, diagnostic_close, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ui.diagnostic, LV_OBJ_FLAG_HIDDEN);
}

esp_err_t sound_key_ui_start(void)
{
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD),
                        TAG, "LCD init failed");
    esp_err_t touch_ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_LCD_TOUCH);
    if (touch_ret != ESP_OK) ESP_LOGW(TAG, "Touch input: %s", esp_err_to_name(touch_ret));
    dev_display_lcd_handles_t *lcd = NULL;
    dev_display_lcd_config_t *cfg = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD,
                                                            (void **)&lcd), TAG, "LCD handle");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD,
                                                            (void **)&cfg), TAG, "LCD config");
    ESP_RETURN_ON_FALSE(lcd != NULL && cfg != NULL && cfg->lcd_width == SCREEN_W &&
                        cfg->lcd_height == SCREEN_H, ESP_ERR_NOT_SUPPORTED,
                        TAG, "expected Korvo 800x480 LCD");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16 * 1024;
    port_cfg.task_affinity = 0;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL init");
    lvgl_port_display_cfg_t display_cfg = {
        .io_handle = lcd->io_handle,
        .panel_handle = lcd->panel_handle,
        .buffer_size = SCREEN_W * (SCREEN_H / 2),
        .double_buffer = true,
        .hres = SCREEN_W,
        .vres = SCREEN_H,
        .monochrome = false,
        .rotation = {.swap_xy = cfg->swap_xy, .mirror_x = cfg->mirror_x,
                     .mirror_y = cfg->mirror_y},
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {.buff_dma = false, .buff_spiram = true},
    };
    lv_display_t *display = NULL;
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
    if (strcmp(cfg->sub_type, "rgb") == 0) {
        const lvgl_port_display_rgb_cfg_t rgb_cfg = {
            .flags = {.bb_mode = cfg->sub_cfg.rgb.panel_config.bounce_buffer_size_px > 0,
                      .avoid_tearing = cfg->sub_cfg.rgb.panel_config.num_fbs > 1},
        };
        display_cfg.flags.direct_mode = rgb_cfg.flags.avoid_tearing;
        display = lvgl_port_add_disp_rgb(&display_cfg, &rgb_cfg);
    }
#endif
    if (display == NULL) display = lvgl_port_add_disp(&display_cfg);
    ESP_RETURN_ON_FALSE(display != NULL, ESP_FAIL, TAG, "LVGL display");
#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUPPORT
    dev_lcd_touch_handles_t *touch = NULL;
    if (touch_ret == ESP_OK &&
        esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH,
                                            (void **)&touch) == ESP_OK &&
        touch != NULL && touch->touch_handle != NULL) {
        const lvgl_port_touch_cfg_t touch_cfg = {.disp = display, .handle = touch->touch_handle};
        (void)lvgl_port_add_touch(&touch_cfg);
    }
#endif
    ESP_RETURN_ON_FALSE(lvgl_port_lock(5000), ESP_ERR_TIMEOUT, TAG, "LVGL lock");
    create_interface(lv_display_get_screen_active(display));
    sound_key_snapshot_t snap;
    sound_key_link_get_snapshot(&snap);
    render_snapshot(&snap);
    lv_timer_create(ui_timer, SK_TELEMETRY_INTERVAL_MS, NULL);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Korvo sound-key interface ready; unlock authority follows telemetry backend");
    return ESP_OK;
}

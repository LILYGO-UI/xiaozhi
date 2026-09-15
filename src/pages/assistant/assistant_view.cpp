#include "pages/assistant/assistant_view.hpp"

#include "components/components.hpp"

#include <cm0/typography.h>

#include <algorithm>
#include <cstdio>

namespace {

constexpr int home_safe_area = 41;
constexpr const char *icon_microphone = "\xEF\x84\xB0";

lv_obj_t *container(lv_obj_t *parent, const char *name)
{
    auto *object = lv_obj_create(parent);
    lv_obj_set_name_static(object, name);
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

lv_obj_t *label(lv_obj_t *parent, const char *name, const char *text, const lv_font_t *font,
                std::uint32_t color = components::color_text)
{
    auto *object = lv_label_create(parent);
    lv_obj_set_name_static(object, name);
    lv_label_set_text(object, text);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

void background(lv_obj_t *object, std::uint32_t color)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
}

void column(lv_obj_t *object, int gap)
{
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(object, gap, 0);
}

void row(lv_obj_t *object, int gap)
{
    lv_obj_set_flex_flow(object, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(object, gap, 0);
    lv_obj_set_flex_align(object, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

lv_obj_t *button(lv_obj_t *parent, const char *name, const char *text, const lv_font_t *font,
                 lv_event_cb_t callback, void *data)
{
    auto *object = lv_button_create(parent);
    lv_obj_set_name_static(object, name);
    lv_obj_set_height(object, 48);
    lv_obj_set_style_radius(object, 16, 0);
    lv_obj_set_style_pad_all(object, 10, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    background(object, components::color_page_background);
    lv_obj_set_style_bg_color(object, lv_color_hex(components::color_accent_soft), LV_STATE_PRESSED);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, data);
    auto *caption = label(object, "assistant_button_caption", text, font, components::color_container_title);
    lv_obj_center(caption);
    return object;
}

bool failed(AssistantState state)
{
    return state == AssistantState::unavailable || state == AssistantState::capture_error ||
           state == AssistantState::error;
}

std::uint32_t state_color(AssistantState state)
{
    if (failed(state)) return components::color_danger;
    if (state == AssistantState::listening || state == AssistantState::recording || state == AssistantState::speaking)
        return components::color_accent;
    return components::color_status_wait;
}

} // namespace

AssistantView::AssistantView(AssistantViewModel &view_model) noexcept : view_model_(view_model) {}

lv_obj_t *AssistantView::create(lv_obj_t *parent, const char *application_name)
{
    if (surface_) return surface_;
    font14_ = lilygo_ui_font_get(14);
    font22_ = lilygo_ui_font_get(22);
    font28_ = lilygo_ui_font_get(28);
    font36_ = lilygo_ui_font_get(36);
    font48_ = lilygo_ui_font_get(48);
    if (!font14_ || !font22_ || !font28_ || !font36_ || !font48_) {
        std::fprintf(stderr, "xiaozhi-ui: required AppKit fonts unavailable\n");
        return nullptr;
    }
    surface_ = components::create_page(parent);
    lv_obj_set_style_pad_bottom(surface_, home_safe_area, 0);
    lv_obj_add_event_cb(surface_, size_changed_event, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(surface_, key_event, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(surface_, deleted_event, LV_EVENT_DELETE, this);
    create_header(application_name);
    main_ = container(surface_, "assistant_main");
    lv_obj_set_width(main_, LV_PCT(100));
    lv_obj_set_flex_grow(main_, 1);
    background(main_, components::color_container_background);
    create_face();
    create_subtitle();
    create_actions();
    create_history();

    view_model_.start();
    timer_ = lv_timer_create(timer_event, 160, this);
    lv_obj_update_layout(surface_);
    apply_layout();
    refresh();
    return surface_;
}

void AssistantView::destroy() noexcept
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;
    view_model_.stop();
    if (surface_) lv_obj_delete(surface_);
    surface_ = header_ = title_ = status_dot_ = status_label_ = main_ = nullptr;
    face_stage_ = face_halo_ = face_ = left_eye_ = right_eye_ = left_cheek_ = right_cheek_ = nullptr;
    mouth_ = open_mouth_ = subtitle_panel_ = subtitle_scroll_ = subtitle_ = speaker_ = detail_label_ = nullptr;
    activation_panel_ = activation_code_ = action_panel_ = mode_row_ = mode_label_ = continuous_switch_ = nullptr;
    talk_row_ = ptt_button_ = ptt_caption_ = interrupt_button_ = retry_button_ = history_button_ = nullptr;
    history_overlay_ = history_header_ = history_title_ = history_back_ = new_chat_button_ = nullptr;
    chat_scroll_ = history_empty_ = nullptr;
    message_rows_.clear();
    rendered_revision_ = 0;
    history_was_visible_ = false;
    history_follow_bottom_ = true;
    binding_layout_ = ptt_pressed_ = false;
    rendered_subtitle_.clear();
}

void AssistantView::create_header(const char *application_name)
{
    header_ = container(surface_, "assistant_header");
    lv_obj_set_size(header_, LV_PCT(100), 84);
    background(header_, components::color_container_background);
    lv_obj_set_style_pad_hor(header_, 24, 0);
    row(header_, 12);
    auto *identity = container(header_, "assistant_identity");
    lv_obj_set_height(identity, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(identity, 1);
    column(identity, 4);
    title_ = label(identity, "app_title", application_name, font28_, components::color_container_title);
    lv_obj_set_width(title_, LV_PCT(100));
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    auto *state_row = container(identity, "assistant_state_row");
    lv_obj_set_size(state_row, LV_PCT(100), LV_SIZE_CONTENT);
    row(state_row, 7);
    status_dot_ = container(state_row, "assistant_status_dot");
    lv_obj_set_size(status_dot_, 7, 7);
    lv_obj_set_style_radius(status_dot_, LV_RADIUS_CIRCLE, 0);
    status_label_ = label(state_row, "assistant_status", "", font14_, components::color_muted);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_DOT);
    lv_label_bind_text(status_label_, view_model_.status_subject(), nullptr);
    history_button_ = button(header_, "assistant_history", "History", font14_, history_event, this);
    lv_obj_set_width(history_button_, 96);
}

void AssistantView::create_face()
{
    face_stage_ = container(main_, "assistant_face_stage");
    lv_obj_add_event_cb(face_stage_, face_size_event, LV_EVENT_SIZE_CHANGED, this);
    face_halo_ = container(face_stage_, "assistant_face_halo");
    background(face_halo_, components::color_face_halo);
    lv_obj_set_style_radius(face_halo_, LV_RADIUS_CIRCLE, 0);
    face_ = container(face_halo_, "assistant_face");
    background(face_, components::color_face_skin);
    lv_obj_set_style_radius(face_, LV_RADIUS_CIRCLE, 0);
    left_cheek_ = container(face_, "assistant_face_left_cheek");
    right_cheek_ = container(face_, "assistant_face_right_cheek");
    left_eye_ = container(face_, "assistant_face_left_eye");
    right_eye_ = container(face_, "assistant_face_right_eye");
    for (auto *cheek : {left_cheek_, right_cheek_}) {
        background(cheek, components::color_face_cheek);
        lv_obj_set_style_radius(cheek, LV_RADIUS_CIRCLE, 0);
    }
    for (auto *eye : {left_eye_, right_eye_}) {
        background(eye, components::color_face_ink);
        lv_obj_set_style_radius(eye, LV_RADIUS_CIRCLE, 0);
    }
    mouth_ = lv_arc_create(face_);
    lv_obj_set_name_static(mouth_, "assistant_face_mouth");
    lv_obj_remove_style_all(mouth_);
    lv_obj_remove_flag(mouth_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(mouth_, lv_color_hex(components::color_face_ink), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(mouth_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(mouth_, true, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(mouth_, 0, 360);
    open_mouth_ = container(face_, "assistant_face_open_mouth");
    background(open_mouth_, components::color_face_ink);
    lv_obj_set_style_radius(open_mouth_, LV_RADIUS_CIRCLE, 0);
}

void AssistantView::create_subtitle()
{
    subtitle_panel_ = container(main_, "assistant_subtitle_panel");
    column(subtitle_panel_, 12);
    lv_obj_set_style_pad_all(subtitle_panel_, 24, 0);
    speaker_ = label(subtitle_panel_, "assistant_speaker", "Xiaozhi", font14_, components::color_muted);
    lv_obj_set_width(speaker_, LV_PCT(100));
    lv_label_bind_text(speaker_, view_model_.speaker_subject(), nullptr);
    subtitle_scroll_ = container(subtitle_panel_, "assistant_subtitle_scroll");
    lv_obj_set_width(subtitle_scroll_, LV_PCT(100));
    lv_obj_set_flex_grow(subtitle_scroll_, 1);
    lv_obj_add_flag(subtitle_scroll_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(subtitle_scroll_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(subtitle_scroll_, LV_SCROLLBAR_MODE_AUTO);
    column(subtitle_scroll_, 12);
    subtitle_ = label(subtitle_scroll_, "assistant_subtitle", "", font28_, components::color_container_title);
    lv_obj_set_width(subtitle_, LV_PCT(100));
    lv_label_set_long_mode(subtitle_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(subtitle_, 6, 0);
    lv_label_bind_text(subtitle_, view_model_.subtitle_subject(), nullptr);
    detail_label_ = label(subtitle_scroll_, "assistant_detail", "", font14_, components::color_muted);
    lv_obj_set_width(detail_label_, LV_PCT(100));
    lv_label_set_long_mode(detail_label_, LV_LABEL_LONG_WRAP);
    lv_label_bind_text(detail_label_, view_model_.detail_subject(), nullptr);

    activation_panel_ = container(subtitle_scroll_, "assistant_activation_panel");
    lv_obj_set_size(activation_panel_, LV_PCT(100), LV_SIZE_CONTENT);
    background(activation_panel_, components::color_warning_soft);
    lv_obj_set_style_radius(activation_panel_, 18, 0);
    lv_obj_set_style_pad_all(activation_panel_, 18, 0);
    column(activation_panel_, 8);
    auto *hint = label(activation_panel_, "assistant_activation_hint", "Activation code", font14_, components::color_warning_text);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    activation_code_ = label(activation_panel_, "assistant_activation_code", "", font48_, components::color_warning_text);
    lv_obj_set_width(activation_code_, LV_PCT(100));
    lv_obj_set_style_text_align(activation_code_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_bind_text(activation_code_, view_model_.activation_subject(), nullptr);
    auto *site = label(activation_panel_, "assistant_activation_site", "xiaozhi.me > Add device", font14_,
                       components::color_warning_text);
    lv_obj_set_width(site, LV_PCT(100));
    lv_label_set_long_mode(site, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(site, LV_TEXT_ALIGN_CENTER, 0);
    retry_button_ = button(subtitle_scroll_, "assistant_retry", "Reconnect", font22_, retry_event, this);
    lv_obj_set_size(retry_button_, LV_PCT(100), 48);
}

void AssistantView::create_actions()
{
    action_panel_ = container(surface_, "assistant_actions");
    background(action_panel_, components::color_container_background);
    lv_obj_set_width(action_panel_, LV_PCT(100));
    lv_obj_set_height(action_panel_, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(action_panel_, 24, 0);
    lv_obj_set_style_pad_ver(action_panel_, 12, 0);
    column(action_panel_, 12);
    mode_row_ = container(action_panel_, "assistant_mode_row");
    lv_obj_set_size(mode_row_, LV_PCT(100), 40);
    row(mode_row_, 10);
    mode_label_ = label(mode_row_, "assistant_mode_label", "Continuous chat", font14_, components::color_muted);
    lv_obj_set_flex_grow(mode_label_, 1);
    continuous_switch_ = lv_switch_create(mode_row_);
    lv_obj_set_name_static(continuous_switch_, "assistant_continuous");
    lv_obj_set_size(continuous_switch_, 52, 32);
    lv_obj_set_ext_click_area(continuous_switch_, 8);
    lv_obj_set_style_bg_color(continuous_switch_, lv_color_hex(components::color_accent),
                               LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(continuous_switch_, continuous_event, LV_EVENT_VALUE_CHANGED, this);
    talk_row_ = container(action_panel_, "assistant_talk_row");
    lv_obj_set_size(talk_row_, LV_PCT(100), 64);
    row(talk_row_, 10);
    ptt_button_ = lv_button_create(talk_row_);
    lv_obj_set_name_static(ptt_button_, "assistant_ptt");
    lv_obj_set_height(ptt_button_, LV_PCT(100));
    lv_obj_set_flex_grow(ptt_button_, 1);
    lv_obj_set_style_radius(ptt_button_, 22, 0);
    lv_obj_set_style_shadow_width(ptt_button_, 0, 0);
    lv_obj_set_style_pad_all(ptt_button_, 0, 0);
    lv_obj_add_event_cb(ptt_button_, ptt_event, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(ptt_button_, ptt_event, LV_EVENT_RELEASED, this);
    lv_obj_add_event_cb(ptt_button_, ptt_event, LV_EVENT_PRESS_LOST, this);
    ptt_caption_ = label(ptt_button_, "assistant_ptt_caption", "", font22_, components::color_container_background);
    lv_obj_set_width(ptt_caption_, LV_PCT(100));
    lv_label_set_long_mode(ptt_caption_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(ptt_caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(ptt_caption_);
    interrupt_button_ = button(talk_row_, "assistant_interrupt", "Stop", font22_, interrupt_event, this);
    lv_obj_set_size(interrupt_button_, 88, LV_PCT(100));
}

void AssistantView::create_history()
{
    history_overlay_ = container(surface_, "assistant_history_overlay");
    lv_obj_add_flag(history_overlay_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    background(history_overlay_, components::color_page_background);
    column(history_overlay_, 0);
    lv_obj_add_flag(history_overlay_, LV_OBJ_FLAG_HIDDEN);
    history_header_ = container(history_overlay_, "assistant_history_header");
    lv_obj_set_size(history_header_, LV_PCT(100), 76);
    lv_obj_set_style_pad_hor(history_header_, 20, 0);
    background(history_header_, components::color_container_background);
    row(history_header_, 10);
    history_back_ = button(history_header_, "assistant_history_back", LV_SYMBOL_LEFT, font22_, close_history_event, this);
    lv_obj_set_width(history_back_, 48);
    history_title_ = label(history_header_, "assistant_history_title", "History", font22_, components::color_container_title);
    lv_obj_set_flex_grow(history_title_, 1);
    lv_label_set_long_mode(history_title_, LV_LABEL_LONG_DOT);
    new_chat_button_ = button(history_header_, "assistant_new_chat", "New chat", font14_, new_chat_event, this);
    lv_obj_set_width(new_chat_button_, 78);
    chat_scroll_ = container(history_overlay_, "assistant_chat");
    lv_obj_set_width(chat_scroll_, LV_PCT(100));
    lv_obj_set_flex_grow(chat_scroll_, 1);
    lv_obj_set_style_pad_all(chat_scroll_, 20, 0);
    column(chat_scroll_, 14);
    lv_obj_add_flag(chat_scroll_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(chat_scroll_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(chat_scroll_, LV_SCROLLBAR_MODE_AUTO);
    history_empty_ = label(chat_scroll_, "assistant_history_empty", "No messages yet.", font22_,
                           components::color_muted);
    lv_obj_set_width(history_empty_, LV_PCT(100));
    lv_obj_set_style_text_align(history_empty_, LV_TEXT_ALIGN_CENTER, 0);
}

void AssistantView::apply_layout() noexcept
{
    if (!surface_ || !history_overlay_ || applying_layout_) return;
    applying_layout_ = true;
    const int width = lv_obj_get_content_width(surface_);
    const int height = lv_obj_get_content_height(surface_);
    const bool compact = width < 400 || height < 500;
    const bool short_view = height < 360;
    wide_ = (width >= 680 || (width >= 420 && short_view)) && width > height;
    binding_layout_ = lv_subject_get_string(view_model_.activation_subject())[0] != '\0';
    const int margin = compact ? 14 : 24;
    const auto history_y = lv_obj_get_scroll_y(chat_scroll_);
    lv_obj_set_style_pad_hor(header_, margin, 0);
    lv_obj_set_height(header_, short_view ? 60 : (compact || wide_ ? 72 : 88));
    lv_obj_set_style_text_font(title_, compact ? font22_ : font28_, 0);
    lv_obj_set_style_pad_hor(action_panel_, margin, 0);
    lv_obj_set_style_pad_ver(action_panel_, compact ? 8 : 12, 0);
    lv_obj_set_style_pad_gap(action_panel_, compact ? 6 : 12, 0);
    lv_obj_set_height(talk_row_, compact ? 56 : 64);
    if (wide_) {
        row(main_, 0);
        lv_obj_set_size(face_stage_, LV_PCT(short_view ? 28 : 38), LV_PCT(100));
        lv_obj_set_size(subtitle_panel_, 0, LV_PCT(100));
        lv_obj_set_flex_grow(subtitle_panel_, 1);
        row(action_panel_, short_view ? 16 : 24);
        lv_obj_set_size(mode_row_, short_view ? 176 : 190, 40);
        lv_obj_set_flex_grow(mode_row_, 0);
        lv_obj_set_width(talk_row_, 0);
        lv_obj_set_flex_grow(talk_row_, 1);
    } else {
        column(main_, 0);
        lv_obj_set_size(face_stage_, LV_PCT(100), LV_PCT(binding_layout_ && compact ? 20 : (compact ? 35 : 47)));
        lv_obj_set_size(subtitle_panel_, LV_PCT(100), 0);
        lv_obj_set_flex_grow(subtitle_panel_, 1);
        column(action_panel_, compact ? 6 : 12);
        lv_obj_set_size(mode_row_, LV_PCT(100), compact ? 34 : 40);
        lv_obj_set_flex_grow(mode_row_, 0);
        lv_obj_set_width(talk_row_, LV_PCT(100));
        lv_obj_set_flex_grow(talk_row_, 0);
    }
    lv_obj_set_style_pad_hor(subtitle_panel_, margin, 0);
    lv_obj_set_style_pad_ver(subtitle_panel_, compact ? 8 : 18, 0);
    lv_obj_set_style_pad_row(subtitle_panel_, compact ? 6 : 12, 0);
    lv_obj_set_style_text_font(subtitle_, compact ? font22_ : font28_, 0);
    lv_obj_set_style_text_align(speaker_, wide_ ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(subtitle_, wide_ ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_flag(face_stage_, LV_OBJ_FLAG_HIDDEN, binding_layout_ && compact);
    lv_obj_set_flag(speaker_, LV_OBJ_FLAG_HIDDEN, binding_layout_ && compact);
    lv_obj_set_style_text_font(activation_code_, compact ? font36_ : font48_, 0);
    const bool short_binding = binding_layout_ && short_view && wide_;
    lv_obj_set_style_pad_all(activation_panel_, short_binding ? 10 : (compact ? 12 : 18), 0);
    lv_obj_set_style_pad_row(activation_panel_, short_binding ? 6 : 8, 0);
    lv_obj_set_flag(lv_obj_find_by_name(activation_panel_, "assistant_activation_hint"),
                    LV_OBJ_FLAG_HIDDEN, short_binding);
    // A short landscape parent has room beside the code, but not underneath it.
    // Keep the refresh action fully visible without shrinking its label or hit area.
    if (short_binding) {
        row(subtitle_scroll_, 12);
        lv_obj_set_width(activation_panel_, LV_PCT(65));
        lv_obj_set_width(retry_button_, 0);
        lv_obj_set_flex_grow(retry_button_, 1);
    } else {
        column(subtitle_scroll_, 12);
        lv_obj_set_flex_align(subtitle_scroll_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_width(activation_panel_, LV_PCT(100));
        lv_obj_set_width(retry_button_, LV_PCT(100));
        lv_obj_set_flex_grow(retry_button_, 0);
    }
    lv_obj_set_size(history_overlay_, width, height);
    lv_obj_set_pos(history_overlay_, 0, 0);
    lv_obj_set_style_pad_hor(history_header_, margin, 0);
    lv_obj_set_style_pad_all(chat_scroll_, margin, 0);
    lv_obj_update_layout(surface_);
    lv_obj_scroll_to_y(chat_scroll_, history_y, LV_ANIM_OFF);
    refresh_face();
    applying_layout_ = false;
}

void AssistantView::refresh()
{
    if (!surface_) return;
    const auto state = view_model_.state();
    const bool recording = state == AssistantState::recording;
    const bool interruptible = state == AssistantState::thinking || state == AssistantState::speaking ||
                               view_model_.continuous();
    const bool binding = lv_subject_get_string(view_model_.activation_subject())[0] != '\0';
    if (binding != binding_layout_) apply_layout();
    background(status_dot_, state_color(state));
    background(ptt_button_, recording ? components::color_danger : components::color_action);
    lv_obj_set_style_bg_color(ptt_button_, lv_color_hex(components::color_accent), LV_STATE_PRESSED);
    const std::string ptt_text = std::string(icon_microphone) + (recording && ptt_pressed_ ? "  Release to send" : "  Hold to talk");
    lv_label_set_text(ptt_caption_, ptt_text.c_str());
    lv_obj_set_flag(interrupt_button_, LV_OBJ_FLAG_HIDDEN, !interruptible);
    lv_obj_set_flag(activation_panel_, LV_OBJ_FLAG_HIDDEN, !binding);
    lv_obj_set_flag(retry_button_, LV_OBJ_FLAG_HIDDEN, !binding && !failed(state) && state != AssistantState::reconnecting);
    lv_label_set_text(lv_obj_get_child(retry_button_, 0), binding ? "Refresh code" : "Reconnect");
    lv_obj_set_state(continuous_switch_, LV_STATE_CHECKED, view_model_.continuous());
    // A binding code is the primary content until the device has been linked.
    lv_obj_set_flag(subtitle_, LV_OBJ_FLAG_HIDDEN, binding);
    lv_obj_set_flag(detail_label_, LV_OBJ_FLAG_HIDDEN, binding || (!failed(state) && state != AssistantState::reconnecting));
    const std::string subtitle_text = lv_subject_get_string(view_model_.subtitle_subject());
    if (subtitle_text != rendered_subtitle_) {
        lv_obj_scroll_to_y(subtitle_scroll_, 0, LV_ANIM_OFF);
        rendered_subtitle_ = subtitle_text;
    }
    if (view_model_.revision() != rendered_revision_) {
        refresh_messages();
        rendered_revision_ = view_model_.revision();
    }
    const bool history_visible = view_model_.history_visible();
    if (history_visible != history_was_visible_) {
        if (!history_visible) history_follow_bottom_ = lv_obj_get_scroll_bottom(chat_scroll_) <= 12;
        lv_obj_set_flag(history_overlay_, LV_OBJ_FLAG_HIDDEN, !history_visible);
        if (history_visible) {
            lv_obj_move_foreground(history_overlay_);
            lv_obj_update_layout(history_overlay_);
            if (history_follow_bottom_) lv_obj_scroll_to_y(chat_scroll_, LV_COORD_MAX, LV_ANIM_OFF);
        }
        history_was_visible_ = history_visible;
    }
    lv_obj_update_layout(surface_);
    refresh_face();
}

void AssistantView::refresh_messages()
{
    const auto &messages = view_model_.messages();
    const bool follow = history_was_visible_ ? lv_obj_get_scroll_bottom(chat_scroll_) <= 12 : history_follow_bottom_;
    const auto old_scroll = lv_obj_get_scroll_y(chat_scroll_);
    std::uint64_t anchor_id = 0;
    int anchor_offset = 0;
    lv_area_t viewport;
    lv_obj_get_coords(chat_scroll_, &viewport);
    if (!follow) {
        for (const auto &entry : message_rows_) {
            lv_area_t area;
            lv_obj_get_coords(entry.row, &area);
            if (area.y2 >= viewport.y1) {
                anchor_id = entry.id;
                anchor_offset = area.y1 - viewport.y1;
                break;
            }
        }
    }
    for (auto it = message_rows_.begin(); it != message_rows_.end();) {
        const auto found = std::find_if(messages.begin(), messages.end(),
                                        [&](const auto &message) { return message.id == it->id; });
        if (found == messages.end()) {
            lv_obj_delete(it->row);
            it = message_rows_.erase(it);
        } else {
            if (it->text != found->text) {
                lv_label_set_text(it->label, found->text.c_str());
                it->text = found->text;
            }
            ++it;
        }
    }
    for (const auto &message : messages) {
        if (std::any_of(message_rows_.begin(), message_rows_.end(),
                         [&](const auto &entry) { return entry.id == message.id; })) continue;
        auto *message_row = container(chat_scroll_, "assistant_message_row");
        lv_obj_set_size(message_row, LV_PCT(100), LV_SIZE_CONTENT);
        column(message_row, 6);
        const bool user = message.role == MessageRole::user;
        const bool notice = message.role == MessageRole::notice;
        auto *who = label(message_row, "assistant_message_speaker", user ? "You" : (notice ? "Notice" : "Xiaozhi"), font14_,
                          components::color_muted);
        lv_obj_set_width(who, LV_PCT(100));
        lv_obj_set_style_text_align(who, user ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT, 0);
        auto *bubble = container(message_row, notice ? "assistant_notice_bubble" :
                                              (user ? "assistant_user_bubble" : "assistant_reply_bubble"));
        lv_obj_set_size(bubble, LV_PCT(100), LV_SIZE_CONTENT);
        background(bubble, user ? components::color_accent_soft : components::color_container_background);
        lv_obj_set_style_radius(bubble, 18, 0);
        lv_obj_set_style_pad_all(bubble, 16, 0);
        auto *text = label(bubble, "assistant_message_text", message.text.c_str(), notice ? font14_ : font22_,
                           notice ? components::color_muted : components::color_text);
        lv_obj_set_width(text, LV_PCT(100));
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        message_rows_.push_back({message.id, message.text, message_row, text});
    }
    lv_obj_set_flag(history_empty_, LV_OBJ_FLAG_HIDDEN, !messages.empty());
    lv_obj_update_layout(chat_scroll_);
    if (follow || messages.empty()) {
        lv_obj_scroll_to_y(chat_scroll_, messages.empty() ? 0 : LV_COORD_MAX, LV_ANIM_OFF);
    } else {
        const auto anchor = std::find_if(message_rows_.begin(), message_rows_.end(),
                                         [&](const auto &entry) { return entry.id == anchor_id; });
        if (anchor != message_rows_.end()) {
            lv_area_t area;
            lv_obj_get_coords(anchor->row, &area);
            lv_obj_scroll_to_y(chat_scroll_, lv_obj_get_scroll_y(chat_scroll_) + area.y1 - viewport.y1 - anchor_offset,
                                LV_ANIM_OFF);
        } else {
            lv_obj_scroll_to_y(chat_scroll_, old_scroll, LV_ANIM_OFF);
        }
    }
    history_follow_bottom_ = follow || messages.empty();
}

void AssistantView::refresh_face()
{
    if (!face_stage_ || !open_mouth_) return;
    const int available = std::min(lv_obj_get_content_width(face_stage_), lv_obj_get_content_height(face_stage_));
    const int halo_size = std::clamp(available - 12, 24, 340);
    const int diameter = halo_size * 84 / 100;
    lv_obj_set_size(face_halo_, halo_size, halo_size);
    lv_obj_center(face_halo_);
    lv_obj_set_size(face_, diameter, diameter);
    lv_obj_center(face_);
    const auto state = view_model_.state();
    const auto &emotion = view_model_.emotion();
    const bool happy = emotion.find("happy") != std::string::npos || emotion.find("laugh") != std::string::npos ||
                       emotion.find("love") != std::string::npos || emotion.find("funny") != std::string::npos;
    const bool sad = emotion.find("sad") != std::string::npos || emotion.find("cry") != std::string::npos || failed(state);
    const bool surprised = emotion.find("surpris") != std::string::npos || emotion.find("shock") != std::string::npos;
    const bool sleepy = emotion.find("sleep") != std::string::npos || emotion.find("tired") != std::string::npos;
    const bool angry = emotion.find("angry") != std::string::npos || emotion.find("annoy") != std::string::npos;
    const bool thinking = state == AssistantState::thinking || emotion.find("think") != std::string::npos ||
                          emotion.find("confus") != std::string::npos;
    const unsigned phase = (lv_tick_get() / 160) % 28;
    const bool blink = phase == 0 || sleepy;
    const int eye_width = std::max(3, diameter * (happy ? 10 : 7) / 100);
    const int eye_height = std::max(3, diameter * (blink || happy ? 3 : (angry ? 7 : (surprised ? 16 : 12))) / 100);
    const int look_x = thinking ? diameter / 35 : 0;
    for (auto *eye : {left_eye_, right_eye_}) lv_obj_set_size(eye, eye_width, eye_height);
    lv_obj_align(left_eye_, LV_ALIGN_CENTER, -diameter * 19 / 100 + look_x, -diameter * 8 / 100);
    lv_obj_align(right_eye_, LV_ALIGN_CENTER, diameter * 19 / 100 + look_x, -diameter * 8 / 100);
    lv_obj_set_style_transform_rotation(left_eye_, angry ? -220 : 0, 0);
    lv_obj_set_style_transform_rotation(right_eye_, angry ? 220 : 0, 0);
    for (auto *cheek : {left_cheek_, right_cheek_}) lv_obj_set_size(cheek, diameter * 15 / 100, diameter * 7 / 100);
    lv_obj_align(left_cheek_, LV_ALIGN_CENTER, -diameter * 29 / 100, diameter * 12 / 100);
    lv_obj_align(right_cheek_, LV_ALIGN_CENTER, diameter * 29 / 100, diameter * 12 / 100);
    const int mouth_size = diameter * (happy ? 42 : 32) / 100;
    lv_obj_set_size(mouth_, mouth_size, mouth_size);
    lv_obj_set_style_arc_width(mouth_, std::max(3, diameter / 40), LV_PART_INDICATOR);
    lv_arc_set_angles(mouth_, sad || angry ? 205 : 25, sad || angry ? 335 : 155);
    lv_obj_align(mouth_, LV_ALIGN_CENTER, 0, diameter * (sad || angry ? 29 : 9) / 100);
    const bool open = surprised || state == AssistantState::speaking || state == AssistantState::recording;
    lv_obj_set_flag(mouth_, LV_OBJ_FLAG_HIDDEN, open);
    lv_obj_set_flag(open_mouth_, LV_OBJ_FLAG_HIDDEN, !open);
    lv_obj_set_size(open_mouth_, diameter * (surprised ? 13 : 17) / 100,
                    diameter * (state == AssistantState::speaking ? (phase % 3 == 0 ? 9 : 18) : 12) / 100);
    lv_obj_align(open_mouth_, LV_ALIGN_CENTER, 0, diameter * 22 / 100);
}

void AssistantView::timer_event(lv_timer_t *timer)
{
    auto *view = static_cast<AssistantView *>(lv_timer_get_user_data(timer));
    for (const auto pressed : view->hardware_mic_.poll()) {
        view->ptt_pressed_ = pressed;
        if (pressed) view->view_model_.begin_push_to_talk();
        else view->view_model_.end_push_to_talk();
    }
    view->view_model_.tick();
    view->refresh();
}

void AssistantView::size_changed_event(lv_event_t *event)
{
    static_cast<AssistantView *>(lv_event_get_user_data(event))->apply_layout();
}

void AssistantView::face_size_event(lv_event_t *event)
{
    static_cast<AssistantView *>(lv_event_get_user_data(event))->refresh_face();
}

void AssistantView::deleted_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    if (view->timer_) lv_timer_delete(view->timer_);
    view->timer_ = nullptr;
    view->surface_ = nullptr;
}

void AssistantView::ptt_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->ptt_pressed_ = lv_event_get_code(event) == LV_EVENT_PRESSED;
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) view->view_model_.begin_push_to_talk();
    else view->view_model_.end_push_to_talk();
    view->refresh();
}

void AssistantView::history_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.set_history_visible(true);
    view->refresh();
}

void AssistantView::close_history_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.set_history_visible(false);
    view->refresh();
}

void AssistantView::continuous_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.set_continuous(lv_obj_has_state(view->continuous_switch_, LV_STATE_CHECKED));
    view->refresh();
}

void AssistantView::interrupt_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.interrupt();
    view->refresh();
}

void AssistantView::new_chat_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.new_conversation();
    view->refresh();
}

void AssistantView::retry_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    view->view_model_.retry();
    view->refresh();
}

void AssistantView::key_event(lv_event_t *event)
{
    auto *view = static_cast<AssistantView *>(lv_event_get_user_data(event));
    const auto key = lv_event_get_key(event);
    if (key == 'm' || key == 'M') {
        view->ptt_pressed_ = !view->ptt_pressed_;
        if (view->ptt_pressed_) view->view_model_.begin_push_to_talk();
        else view->view_model_.end_push_to_talk();
    }
    if (key == LV_KEY_ESC && view->view_model_.history_visible()) view->view_model_.set_history_visible(false);
    view->refresh();
}

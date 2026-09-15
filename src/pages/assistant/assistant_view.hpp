#ifndef LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_VIEW_HPP
#define LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_VIEW_HPP

#include "pages/assistant/assistant_view_model.hpp"
#include "pages/assistant/hardware_mic_input.hpp"

#include <lvgl.h>
#include <cstdint>
#include <string>
#include <vector>

class AssistantView {
public:
    explicit AssistantView(AssistantViewModel &view_model) noexcept;
    lv_obj_t *create(lv_obj_t *parent, const char *application_name);
    void destroy() noexcept;

private:
    struct MessageRow {
        std::uint64_t id;
        std::string text;
        lv_obj_t *row;
        lv_obj_t *label;
    };
    static void timer_event(lv_timer_t *timer);
    static void size_changed_event(lv_event_t *event);
    static void face_size_event(lv_event_t *event);
    static void deleted_event(lv_event_t *event);
    static void ptt_event(lv_event_t *event);
    static void history_event(lv_event_t *event);
    static void close_history_event(lv_event_t *event);
    static void continuous_event(lv_event_t *event);
    static void interrupt_event(lv_event_t *event);
    static void new_chat_event(lv_event_t *event);
    static void retry_event(lv_event_t *event);
    static void key_event(lv_event_t *event);
    void create_header(const char *application_name);
    void create_face();
    void create_subtitle();
    void create_actions();
    void create_history();
    void apply_layout() noexcept;
    void refresh();
    void refresh_messages();
    void refresh_face();

    AssistantViewModel &view_model_;
    HardwareMicInput hardware_mic_;
    lv_timer_t *timer_ = nullptr;
    const lv_font_t *font14_ = nullptr;
    const lv_font_t *font22_ = nullptr;
    const lv_font_t *font28_ = nullptr;
    const lv_font_t *font36_ = nullptr;
    const lv_font_t *font48_ = nullptr;
    lv_obj_t *surface_ = nullptr;
    lv_obj_t *header_ = nullptr;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *status_dot_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *main_ = nullptr;
    lv_obj_t *face_stage_ = nullptr;
    lv_obj_t *face_halo_ = nullptr;
    lv_obj_t *face_ = nullptr;
    lv_obj_t *left_eye_ = nullptr;
    lv_obj_t *right_eye_ = nullptr;
    lv_obj_t *left_cheek_ = nullptr;
    lv_obj_t *right_cheek_ = nullptr;
    lv_obj_t *mouth_ = nullptr;
    lv_obj_t *open_mouth_ = nullptr;
    lv_obj_t *subtitle_panel_ = nullptr;
    lv_obj_t *subtitle_scroll_ = nullptr;
    lv_obj_t *subtitle_ = nullptr;
    lv_obj_t *speaker_ = nullptr;
    lv_obj_t *detail_label_ = nullptr;
    lv_obj_t *activation_panel_ = nullptr;
    lv_obj_t *activation_code_ = nullptr;
    lv_obj_t *action_panel_ = nullptr;
    lv_obj_t *mode_row_ = nullptr;
    lv_obj_t *mode_label_ = nullptr;
    lv_obj_t *continuous_switch_ = nullptr;
    lv_obj_t *talk_row_ = nullptr;
    lv_obj_t *ptt_button_ = nullptr;
    lv_obj_t *ptt_caption_ = nullptr;
    lv_obj_t *interrupt_button_ = nullptr;
    lv_obj_t *retry_button_ = nullptr;
    lv_obj_t *history_button_ = nullptr;
    lv_obj_t *history_overlay_ = nullptr;
    lv_obj_t *history_header_ = nullptr;
    lv_obj_t *history_title_ = nullptr;
    lv_obj_t *history_back_ = nullptr;
    lv_obj_t *new_chat_button_ = nullptr;
    lv_obj_t *chat_scroll_ = nullptr;
    lv_obj_t *history_empty_ = nullptr;
    std::vector<MessageRow> message_rows_;
    std::uint64_t rendered_revision_ = 0;
    bool applying_layout_ = false;
    bool history_was_visible_ = false;
    bool history_follow_bottom_ = true;
    bool wide_ = false;
    bool binding_layout_ = false;
    bool ptt_pressed_ = false;
    std::string rendered_subtitle_;
};

#endif

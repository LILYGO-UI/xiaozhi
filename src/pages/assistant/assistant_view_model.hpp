#ifndef LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_VIEW_MODEL_HPP
#define LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "pages/assistant/assistant_model.hpp"
#include "pages/assistant/session_controller.hpp"

class AssistantViewModel {
public:
    AssistantViewModel();

    void start();
    void stop() noexcept;
    void tick();
    void begin_push_to_talk();
    void end_push_to_talk();
    void new_conversation();
    void retry();
    void set_history_visible(bool visible);
    void set_continuous(bool enabled);
    void interrupt();

    [[nodiscard]] const std::vector<ChatMessage> &messages() const noexcept;
    [[nodiscard]] AssistantState state() const noexcept;
    [[nodiscard]] const std::string &emotion() const noexcept;
    [[nodiscard]] bool history_visible() const noexcept;
    [[nodiscard]] bool continuous() const noexcept;
    [[nodiscard]] bool wake_word_available() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] lv_subject_t *status_subject() noexcept;
    [[nodiscard]] lv_subject_t *detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *activation_subject() noexcept;
    [[nodiscard]] lv_subject_t *recording_subject() noexcept;
    [[nodiscard]] lv_subject_t *history_visible_subject() noexcept;
    [[nodiscard]] lv_subject_t *continuous_subject() noexcept;
    [[nodiscard]] lv_subject_t *subtitle_subject() noexcept;
    [[nodiscard]] lv_subject_t *speaker_subject() noexcept;

private:
    static const char *state_text(AssistantState state) noexcept;
    void publish();

    AssistantModel model_;
    SessionController session_;
    StringSubject<64> status_text_;
    StringSubject<192> detail_text_;
    StringSubject<32> activation_text_;
    IntSubject recording_;
    IntSubject history_visible_subject_;
    IntSubject continuous_subject_;
    StringSubject<2048> subtitle_text_;
    StringSubject<64> speaker_text_;
    bool history_visible_             = false;
    bool started_                     = false;
    std::uint64_t published_revision_ = 0;
    bool published_continuous_        = false;
};

#endif

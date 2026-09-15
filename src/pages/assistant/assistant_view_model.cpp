#include "pages/assistant/assistant_view_model.hpp"

#include <utility>

AssistantViewModel::AssistantViewModel()
    : status_text_(state_text(model_.state())),
      detail_text_(model_.detail()),
      activation_text_(""),
      recording_(0, 0, 1),
      history_visible_subject_(0, 0, 1),
      continuous_subject_(0, 0, 1),
      subtitle_text_("Hi, I'm Xiaozhi. What would you like to talk about?"),
      speaker_text_("Xiaozhi")
{
    publish();
}

void AssistantViewModel::start()
{
    if (started_) return;
    started_ = true;
    session_.start();
    tick();
}

void AssistantViewModel::stop() noexcept
{
    session_.stop();
    started_ = false;
}

void AssistantViewModel::tick()
{
    for (auto &event : session_.poll()) {
        switch (event.kind) {
            case SessionEventKind::binding_code:
                model_.set_activation_code(event.text);
                break;
            case SessionEventKind::connecting:
                model_.set_state(AssistantState::connecting, std::move(event.text));
                break;
            case SessionEventKind::ready:
                model_.clear_activation_code();
                model_.set_state(AssistantState::listening, std::move(event.text));
                break;
            case SessionEventKind::recording:
            case SessionEventKind::wake_detected:
                model_.set_state(AssistantState::recording, std::move(event.text));
                break;
            case SessionEventKind::thinking:
                model_.set_state(AssistantState::thinking, std::move(event.text));
                break;
            case SessionEventKind::speaking:
                model_.set_state(AssistantState::speaking, std::move(event.text));
                break;
            case SessionEventKind::response_done:
                model_.set_state(AssistantState::listening,
                                 session_.wake_word_available() ? "Say \"Xiaozhi Xiaozhi\" to wake me up." : "Hold to talk again.");
                break;
            case SessionEventKind::reconnecting:
                // Keep the microphone diagnosis visible during the retry delay;
                // the notice also survives the subsequent connection attempt.
                if (model_.state() != AssistantState::capture_error)
                    model_.set_state(AssistantState::reconnecting, std::move(event.text));
                break;
            case SessionEventKind::user_message:
                model_.append_message(MessageRole::user, std::move(event.text));
                break;
            case SessionEventKind::assistant_message:
                model_.append_message(MessageRole::assistant, std::move(event.text));
                break;
            case SessionEventKind::emotion:
                model_.set_emotion(std::move(event.text));
                break;
            case SessionEventKind::continuous_changed:
                // publish() reads the session's final mode after this event batch.
                break;
            case SessionEventKind::unavailable:
                model_.set_state(AssistantState::unavailable, std::move(event.text));
                break;
            case SessionEventKind::capture_error:
                model_.append_message(MessageRole::notice, event.text);
                model_.set_state(AssistantState::capture_error, std::move(event.text));
                break;
            case SessionEventKind::error:
                model_.append_message(MessageRole::notice, event.text);
                model_.set_state(AssistantState::error, std::move(event.text));
                break;
        }
    }
    publish();
}

void AssistantViewModel::begin_push_to_talk()
{
    if (model_.is_recording() && !session_.continuous()) return;
    session_.begin_push_to_talk();
    tick();
}

void AssistantViewModel::end_push_to_talk()
{
    session_.end_push_to_talk();
    tick();
}

void AssistantViewModel::new_conversation()
{
    model_.clear_conversation();
    session_.new_conversation();
    tick();
}

void AssistantViewModel::retry()
{
    model_.set_state(AssistantState::connecting, "Reconnecting");
    session_.retry();
    tick();
}

void AssistantViewModel::set_history_visible(bool visible)
{
    if (history_visible_ == visible) return;
    history_visible_ = visible;
    history_visible_subject_.set(visible ? 1 : 0);
}

void AssistantViewModel::set_continuous(bool enabled)
{
    session_.set_continuous(enabled);
    tick();
}

void AssistantViewModel::interrupt()
{
    session_.interrupt();
    tick();
}

const std::vector<ChatMessage> &AssistantViewModel::messages() const noexcept
{
    return model_.messages();
}

AssistantState AssistantViewModel::state() const noexcept
{
    return model_.state();
}

const std::string &AssistantViewModel::emotion() const noexcept
{
    return model_.emotion();
}

bool AssistantViewModel::history_visible() const noexcept
{
    return history_visible_;
}

bool AssistantViewModel::continuous() const noexcept
{
    return session_.continuous();
}

bool AssistantViewModel::wake_word_available() const noexcept
{
    return session_.wake_word_available();
}

std::uint64_t AssistantViewModel::revision() const noexcept
{
    return model_.revision();
}

lv_subject_t *AssistantViewModel::status_subject() noexcept
{
    return status_text_.get();
}

lv_subject_t *AssistantViewModel::detail_subject() noexcept
{
    return detail_text_.get();
}

lv_subject_t *AssistantViewModel::activation_subject() noexcept
{
    return activation_text_.get();
}

lv_subject_t *AssistantViewModel::recording_subject() noexcept
{
    return recording_.get();
}

lv_subject_t *AssistantViewModel::history_visible_subject() noexcept
{
    return history_visible_subject_.get();
}

lv_subject_t *AssistantViewModel::continuous_subject() noexcept
{
    return continuous_subject_.get();
}

lv_subject_t *AssistantViewModel::subtitle_subject() noexcept
{
    return subtitle_text_.get();
}

lv_subject_t *AssistantViewModel::speaker_subject() noexcept
{
    return speaker_text_.get();
}

const char *AssistantViewModel::state_text(AssistantState state) noexcept
{
    switch (state) {
        case AssistantState::starting:
            return "Starting";
        case AssistantState::binding:
            return "Awaiting activation";
        case AssistantState::connecting:
            return "Connecting";
        case AssistantState::listening:
            return "Ready to talk";
        case AssistantState::recording:
            return "Listening";
        case AssistantState::thinking:
            return "Thinking";
        case AssistantState::speaking:
            return "Speaking";
        case AssistantState::reconnecting:
            return "Reconnecting";
        case AssistantState::unavailable:
            return "Service unavailable";
        case AssistantState::capture_error:
            return "Recording failed";
        case AssistantState::error:
            return "Connection error";
    }
    return "Xiaozhi";
}

void AssistantViewModel::publish()
{
    // The mode is session state and may change without a new chat message.
    if (published_revision_ == model_.revision() && published_continuous_ == session_.continuous()) return;
    published_continuous_ = session_.continuous();
    continuous_subject_.set(published_continuous_ ? 1 : 0);
    published_revision_ = model_.revision();
    status_text_.set(state_text(model_.state()));
    detail_text_.set(model_.detail());
    activation_text_.set(model_.activation_code());
    recording_.set(model_.is_recording() ? 1 : 0);

    const ChatMessage *latest = nullptr;
    for (auto message = model_.messages().rbegin(); message != model_.messages().rend(); ++message) {
        if (message->role != MessageRole::notice) {
            latest = &*message;
            break;
        }
    }
    if (model_.is_recording()) {
        speaker_text_.set("I'm listening");
        subtitle_text_.set(session_.continuous() ? "Go ahead. I'll reply when you pause." : "Go ahead. Release the button to send.");
    } else if (model_.state() == AssistantState::binding) {
        speaker_text_.set("Welcome");
        subtitle_text_.set("Activate your device to start chatting.");
    } else if (latest) {
        speaker_text_.set(latest->role == MessageRole::user ? "You said" : "Xiaozhi");
        subtitle_text_.set(latest->text);
    } else {
        speaker_text_.set("Xiaozhi");
        subtitle_text_.set("Hi, I'm Xiaozhi. What would you like to talk about?");
    }
}

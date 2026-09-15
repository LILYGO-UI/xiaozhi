#ifndef LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_MODEL_HPP
#define LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_ASSISTANT_MODEL_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class AssistantState {
    starting,
    binding,
    connecting,
    listening,
    recording,
    thinking,
    speaking,
    reconnecting,
    unavailable,
    capture_error,
    error,
};

enum class MessageRole { user, assistant, notice };

struct ChatMessage {
    std::uint64_t id = 0;
    MessageRole role = MessageRole::notice;
    std::string text;
};

class AssistantModel {
public:
    static constexpr std::size_t maximum_messages = 80;

    AssistantModel();

    [[nodiscard]] AssistantState state() const noexcept;
    [[nodiscard]] const std::string &detail() const noexcept;
    [[nodiscard]] const std::string &activation_code() const noexcept;
    [[nodiscard]] const std::string &emotion() const noexcept;
    [[nodiscard]] const std::vector<ChatMessage> &messages() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] bool is_recording() const noexcept;
    [[nodiscard]] bool is_bound() const noexcept;

    void set_state(AssistantState state, std::string detail = {});
    bool set_activation_code(std::string_view code);
    void clear_activation_code();
    void set_emotion(std::string emotion);
    void append_message(MessageRole role, std::string text);
    void clear_conversation();

private:
    static std::string clean_text(std::string text);

    AssistantState state_ = AssistantState::starting;
    std::string detail_;
    std::string activation_code_;
    std::string emotion_ = "neutral";
    std::vector<ChatMessage> messages_;
    std::uint64_t next_message_id_ = 1;
    std::uint64_t revision_        = 1;
};

#endif

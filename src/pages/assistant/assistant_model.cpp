#include "pages/assistant/assistant_model.hpp"

#include <algorithm>
#include <cctype>

AssistantModel::AssistantModel()
{
    append_message(MessageRole::notice, "Hold to talk, or turn on continuous chat.");
}

AssistantState AssistantModel::state() const noexcept
{
    return state_;
}

const std::string &AssistantModel::detail() const noexcept
{
    return detail_;
}

const std::string &AssistantModel::activation_code() const noexcept
{
    return activation_code_;
}

const std::string &AssistantModel::emotion() const noexcept
{
    return emotion_;
}

const std::vector<ChatMessage> &AssistantModel::messages() const noexcept
{
    return messages_;
}

std::uint64_t AssistantModel::revision() const noexcept
{
    return revision_;
}

bool AssistantModel::is_recording() const noexcept
{
    return state_ == AssistantState::recording;
}

bool AssistantModel::is_bound() const noexcept
{
    return activation_code_.empty() && state_ != AssistantState::binding;
}

void AssistantModel::set_state(AssistantState state, std::string detail)
{
    detail = clean_text(std::move(detail));
    if (state_ == state && detail_ == detail) return;
    state_  = state;
    detail_ = std::move(detail);
    ++revision_;
}

bool AssistantModel::set_activation_code(std::string_view code)
{
    std::string digits;
    for (const unsigned char character : code) {
        if (std::isdigit(character)) digits.push_back(static_cast<char>(character));
    }
    if (digits.size() != 6) return false;
    if (activation_code_ == digits && state_ == AssistantState::binding) return true;
    activation_code_ = std::move(digits);
    state_           = AssistantState::binding;
    detail_          = "Add a device at xiaozhi.me and enter this code.";
    ++revision_;
    return true;
}

void AssistantModel::clear_activation_code()
{
    if (activation_code_.empty()) return;
    activation_code_.clear();
    ++revision_;
}

void AssistantModel::set_emotion(std::string emotion)
{
    emotion = clean_text(std::move(emotion));
    if (emotion.empty()) emotion = "neutral";
    if (emotion_ == emotion) return;
    emotion_ = std::move(emotion);
    ++revision_;
}

void AssistantModel::append_message(MessageRole role, std::string text)
{
    text = clean_text(std::move(text));
    if (text.empty()) return;
    if (!messages_.empty() && messages_.back().role == role && messages_.back().text == text) return;
    if (messages_.size() == maximum_messages) messages_.erase(messages_.begin());
    messages_.push_back(ChatMessage{next_message_id_++, role, std::move(text)});
    ++revision_;
}

void AssistantModel::clear_conversation()
{
    messages_.clear();
    set_emotion("neutral");
    append_message(MessageRole::notice, "New conversation started.");
}

std::string AssistantModel::clean_text(std::string text)
{
    std::replace(text.begin(), text.end(), '\r', ' ');
    std::replace(text.begin(), text.end(), '\n', ' ');
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    constexpr std::size_t maximum_text_bytes = 1024;
    if (text.size() > maximum_text_bytes) {
        // Keep the complete UTF-8 character before the byte limit.
        auto end = maximum_text_bytes;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        text.resize(end);
    }
    return text;
}

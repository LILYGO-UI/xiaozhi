#include "pages/assistant/assistant_model.hpp"

#include <cassert>

namespace {

void test_model()
{
    AssistantModel model;
    assert(model.state() == AssistantState::starting);
    assert(!model.messages().empty());
    assert(!model.set_activation_code("12A45"));
    assert(model.activation_code().empty());
    assert(model.set_activation_code("设备码 123 456"));
    assert(model.state() == AssistantState::binding);
    assert(model.activation_code() == "123456");
    model.clear_activation_code();
    model.set_state(AssistantState::recording, "松开发送");
    assert(model.is_recording());
    model.append_message(MessageRole::user, "  你好\n");
    assert(model.messages().back().text == "你好");
    const auto count = model.messages().size();
    model.append_message(MessageRole::user, "你好");
    assert(model.messages().size() == count);
    for (std::size_t i = 0; i < AssistantModel::maximum_messages + 10; ++i)
        model.append_message(MessageRole::assistant, std::to_string(i));
    assert(model.messages().size() == AssistantModel::maximum_messages);
    model.clear_conversation();
    assert(model.messages().size() == 1);
    assert(model.messages().front().role == MessageRole::notice);
    std::string long_text;
    for (int i = 0; i < 500; ++i) long_text += "智";
    model.append_message(MessageRole::assistant, long_text);
    assert(model.messages().back().text.size() <= 1024);
    assert(model.messages().back().text.size() % 3 == 0);
    assert(model.messages().back().text == long_text.substr(0, 1023));
}

}  // namespace

int main()
{
    test_model();
    return 0;
}

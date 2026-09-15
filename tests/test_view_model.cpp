#include "pages/assistant/assistant_view_model.hpp"

#include <cassert>

int main()
{
    lv_init();

    {
        StringSubject<8> caption("小智你好");
        assert(std::string(caption.value()) == "小智");
        caption.set("A小智你好");
        assert(std::string(caption.value()) == "A小智");
    }

    {
        AssistantViewModel view_model;
        view_model.start();
        assert(view_model.state() == AssistantState::listening);
        assert(std::string(lv_subject_get_string(view_model.status_subject())) == "Ready to talk");
        view_model.begin_push_to_talk();
        assert(view_model.state() == AssistantState::recording);
        assert(lv_subject_get_int(view_model.recording_subject()) == 1);
        view_model.end_push_to_talk();
        assert(view_model.state() == AssistantState::listening);
        assert(view_model.messages().size() >= 3);
        assert(std::string(lv_subject_get_string(view_model.subtitle_subject())).find("weather") != std::string::npos);
        assert(std::string(lv_subject_get_string(view_model.speaker_subject())) == "Xiaozhi");
        view_model.set_history_visible(true);
        assert(view_model.history_visible());
        assert(lv_subject_get_int(view_model.history_visible_subject()) == 1);
        view_model.set_continuous(true);
        assert(view_model.continuous());
        assert(lv_subject_get_int(view_model.continuous_subject()) == 1);
        view_model.set_continuous(false);
        assert(!view_model.continuous());
        assert(lv_subject_get_int(view_model.continuous_subject()) == 0);
        view_model.set_continuous(true);
        view_model.new_conversation();
        assert(view_model.messages().size() == 1);
        assert(!view_model.continuous());
        assert(view_model.state() == AssistantState::listening);
        assert(std::string(lv_subject_get_string(view_model.subtitle_subject())).find("Hi, I'm Xiaozhi.") != std::string::npos);
        view_model.stop();
    }

    lv_deinit();
    return 0;
}

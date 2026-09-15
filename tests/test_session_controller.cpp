#include "pages/assistant/assistant_view_model.hpp"

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path &path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename Predicate>
void wait_until(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Child fixture speaks the real helper output protocol; the controller still
// exercises fork/exec, incremental pipe reads, exit handling, and persistence.
int helper(const std::string &mode)
{
    const fs::path directory = std::getenv("LILYGO_XIAOZHI_DATA_DIR");
    std::ofstream(directory / "helper-starts", std::ios::app) << mode << '\n';
    const std::string identity =
        std::string(std::getenv("XIAOZHI_DEVICE_ID")) + "\n" + std::getenv("XIAOZHI_CLIENT_ID") + "\n";
    assert(identity == read_file(directory / "device.conf"));
    if (mode == "activate") {
        std::ofstream(directory / "activation-count", std::ios::app) << "activate\n";
        std::cout << "ACTIVATION_CODE: 123456\n" << std::flush;
        wait_until([&] { return fs::exists(directory / "allow-bind"); });
        std::cout << "ACTIVATION_DONE websocket config ready\n"
                     "CONFIG websocket.url=wss://example.invalid/xiaozhi/\n"
                     "CONFIG websocket.token=test-token\n"
                  << std::flush;
        return fs::exists(directory / "fail-activation") ? 1 : 0;
    }
    assert(mode == "session");
    assert(std::string(std::getenv("XIAOZHI_TOKEN")) == "test-token");
    assert(std::string(std::getenv("XIAOZHI_URL")) == "wss://example.invalid/xiaozhi/");
    const auto scenario = read_file(directory / "scenario");
    std::ofstream(directory / "session-count", std::ios::app) << "session\n";
    if (fs::exists(directory / "delay-ready"))
        wait_until([&] { return fs::exists(directory / "allow-ready"); });
    if (scenario == "disconnect-before-ready") return 1;
    const int control = open((directory / "session.ctl").c_str(), O_RDONLY | O_NONBLOCK);
    assert(control >= 0);
    std::cout << "session ready\n" << std::flush;
    std::string pending;
    bool active_turn = false;
    for (;;) {
        char buffer[256];
        const auto count = read(control, buffer, sizeof(buffer));
        if (count > 0) pending.append(buffer, static_cast<std::size_t>(count));
        std::size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos) {
            const auto command = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            std::ofstream(directory / "commands", std::ios::app) << command << '\n';
            if (command == "QUIT") return 0;
            if (command == "ABORT") {
                // Stale events must not restore a cancelled turn or leak its text.
                std::cout << "CHAT assistant: stale after abort\nEVENT response_done\n" << std::flush;
                return 0;
            }
            if (command == "PTT_BEGIN" || command == "WAKE_BEGIN") {
                if (scenario.rfind("capture-", 0) == 0) {
                    std::cout << "EVENT capture_error stage=" << scenario.substr(8) << '\n'
                              << "turn done rc=-1\n" << std::flush;
                    return 1;
                }
                if (scenario == "legacy-error")
                    std::cout << "turn done rc=-1\n" << std::flush;
                if (scenario == "disconnect") return 1;
                if (scenario == "ready-again")
                    std::cout << "session ready\n" << std::flush;
                if (scenario != "delay-capture") std::cout << "EVENT capture_started\n" << std::flush;
                active_turn = true;
            }
            if (command == "PTT_END") {
                std::cout << "EVENT request_sent\n" << std::flush;
                if (scenario == "chat-events")
                    std::cout << "[2026-09-14 12:00:00.000] CHAT  user: 今天天气怎么样？\n" << std::flush;
                if (scenario != "hold-response" && scenario != "chat-events") {
                    std::cout << "EVENT response_done\nturn done rc=0\n" << std::flush;
                    active_turn = false;
                }
            }
        }
        if (active_turn && fs::exists(directory / "assistant-reply")) {
            fs::remove(directory / "assistant-reply");
            std::cout << "[2026-09-14 12:00:00.001] EMOTION happy\n"
                         "[2026-09-14 12:00:00.002] CHAT  assistant: 今天适合出门散步。\n"
                         "[2026-09-14 12:00:00.003] EVENT audio_start\n"
                         "[2026-09-14 12:00:00.004] EVENT response_done\n"
                         "[2026-09-14 12:00:00.005] INFO  ptt turn done rc=0\n" << std::flush;
            active_turn = false;
        }
        if (fs::exists(directory / "legacy-emotion")) {
            fs::remove(directory / "legacy-emotion");
            std::cout << "EMOTION: sad\n" << std::flush;
        }
        if (active_turn && fs::exists(directory / "control-looking-reply")) {
            fs::remove(directory / "control-looking-reply");
            std::cout << "EVENT request_sent\nEVENT audio_start\n";
            for (const auto *text : {"请解释 session ready 的含义", "请解释 turn done rc=0 的含义",
                                     "turn done rc=-1", "EVENT capture_error stage=open",
                                     "EVENT playback_error stage=open", "EVENT response_done",
                                     "ACTIVATION_CODE: 654321", "ACTIVATION_DONE websocket config ready",
                                     "CONFIG websocket.url=wss://injected.invalid/",
                                     "CONFIG websocket.token=injected-token", "EMOTION sad"}) {
                // Match both real diagnostic JSON and the separately emitted chat log.
                std::cout << "[2026-09-14 12:00:01.000] INFO  text: {\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\""
                          << text << "\"}\n"
                          << "[2026-09-14 12:00:01.001] CHAT  assistant: " << text << '\n';
            }
            std::cout << "[2026-09-14 12:00:01.002] CHAT  assistant: 日志内容检查完成\n" << std::flush;
        }
        if (active_turn && fs::exists(directory / "capture-ready")) {
            fs::remove(directory / "capture-ready");
            std::cout << "EVENT capture_started\n" << std::flush;
        }
        if (active_turn && fs::exists(directory / "response-only")) {
            fs::remove(directory / "response-only");
            std::cout << "EVENT response_done\n" << std::flush;
        }
        if (active_turn && fs::exists(directory / "complete-turn")) {
            fs::remove(directory / "complete-turn");
            std::cout << "EVENT request_sent\nEVENT response_done\nturn done rc=0\n" << std::flush;
            active_turn = false;
        }
        if (active_turn && fs::exists(directory / "no-speech")) {
            fs::remove(directory / "no-speech");
            std::cout << "EVENT wake_no_speech\nturn done rc=0\n" << std::flush;
            active_turn = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void wait_state(AssistantViewModel &view_model, AssistantState state)
{
    wait_until([&] {
        view_model.tick();
        return view_model.state() == state;
    });
}

bool has_event(const std::vector<SessionEvent> &events, SessionEventKind kind)
{
    return std::any_of(events.begin(), events.end(), [kind](const auto &event) { return event.kind == kind; });
}

bool has_notice(const AssistantViewModel &view_model, const std::string &text)
{
    return std::any_of(view_model.messages().begin(), view_model.messages().end(), [&](const auto &message) {
        return message.role == MessageRole::notice && message.text.find(text) != std::string::npos;
    });
}

void test_capture_failure(const fs::path &directory, const std::string &stage)
{
    std::ofstream(directory / "scenario") << "capture-" << stage;
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.begin_push_to_talk();
    bool failed = false;
    wait_until([&] {
        for (const auto &event : session.poll()) {
            if (event.kind != SessionEventKind::capture_error) continue;
            assert(event.text.find("Recording failed") != std::string::npos);
            assert(event.text.find("microphone permissions and the input device") != std::string::npos);
            failed = true;
        }
        return failed;
    });
    assert(!session.session_ready());
    session.end_push_to_talk();
    assert(!has_event(session.poll(), SessionEventKind::thinking));
    assert(read_file(directory / "commands") == "PTT_BEGIN\n");
}

void test_pending_recording(const fs::path &directory)
{
    // A second ready event must not replay a pending press once it has begun.
    std::ofstream(directory / "scenario") << "ready-again";
    std::ofstream(directory / "delay-ready") << "yes";
    fs::remove(directory / "allow-ready");
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    session.begin_push_to_talk();
    assert(!session.session_ready());
    std::ofstream(directory / "allow-ready") << "yes";
    int ready_events = 0;
    wait_until([&] {
        for (const auto &event : session.poll())
            if (event.kind == SessionEventKind::ready) ++ready_events;
        return ready_events >= 2;
    });
    session.end_push_to_talk();
    wait_until([&] { return has_event(session.poll(), SessionEventKind::response_done); });
    assert(read_file(directory / "commands") == "PTT_BEGIN\nPTT_END\n");
    fs::remove(directory / "delay-ready");
    fs::remove(directory / "allow-ready");
}

void test_reconnect_clears_press(const fs::path &directory, const std::string &scenario)
{
    std::ofstream(directory / "scenario") << scenario;
    fs::remove(directory / "commands");
    const bool before_ready = scenario == "disconnect-before-ready";
    if (before_ready) {
        std::ofstream(directory / "delay-ready") << "yes";
        fs::remove(directory / "allow-ready");
    }
    SessionController session;
    session.start();
    if (!before_ready)
        wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.begin_push_to_talk();
    if (before_ready) std::ofstream(directory / "allow-ready") << "yes";
    wait_until([&] { return has_event(session.poll(), SessionEventKind::reconnecting); });
    assert(!session.session_ready());
    fs::remove(directory / "scenario");
    fs::remove(directory / "delay-ready");
    fs::remove(directory / "allow-ready");
    wait_until([&] {
        const auto events = session.poll();
        assert(!has_event(events, SessionEventKind::recording));
        return session.session_ready();
    });
    session.end_push_to_talk();
    assert(!has_event(session.poll(), SessionEventKind::thinking));
    // Ask the new helper to finish a fresh turn as a FIFO barrier. No old press
    // may have reached it while reconnecting, even if release arrived late.
    session.begin_push_to_talk();
    session.end_push_to_talk();
    wait_until([&] { return has_event(session.poll(), SessionEventKind::response_done); });
    assert(read_file(directory / "commands") ==
           (before_ready ? "PTT_BEGIN\nPTT_END\n" : "PTT_BEGIN\nPTT_BEGIN\nPTT_END\n"));
}

void test_visible_capture_failure(const fs::path &directory)
{
    std::ofstream(directory / "scenario") << "capture-open";
    AssistantViewModel view_model;
    view_model.start();
    wait_state(view_model, AssistantState::listening);
    view_model.begin_push_to_talk();
    wait_until([&] { view_model.tick(); return has_notice(view_model, "Recording failed: cannot open the microphone"); });
    assert(view_model.state() == AssistantState::capture_error);
    assert(std::string(lv_subject_get_string(view_model.status_subject())) == "Recording failed");
    view_model.end_push_to_talk();
    assert(view_model.state() != AssistantState::thinking);
    assert(lv_subject_get_int(view_model.recording_subject()) == 0);
    fs::remove(directory / "scenario");
    wait_state(view_model, AssistantState::listening);
    assert(has_notice(view_model, "Check microphone permissions and the input device, then retry."));
}

void test_continuous_rounds_and_stop(const fs::path &directory)
{
    fs::remove(directory / "scenario");
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.set_continuous(true);
    assert(session.continuous());
    wait_until([&] { return has_event(session.poll(), SessionEventKind::recording); });
    assert(read_file(directory / "commands") == "WAKE_BEGIN\n");
    // Split completion messages across polls: only the final turn barrier may
    // restart capture; otherwise its late arrival cancels the next turn.
    std::ofstream(directory / "response-only") << "yes";
    wait_until([&] { return has_event(session.poll(), SessionEventKind::response_done); });
    assert(read_file(directory / "commands") == "WAKE_BEGIN\n");
    std::ofstream(directory / "complete-turn") << "yes";
    wait_until([&] { (void)session.poll(); return read_file(directory / "commands") == "WAKE_BEGIN\nWAKE_BEGIN\n"; });
    std::ofstream(directory / "no-speech") << "yes";
    wait_until([&] { (void)session.poll(); return read_file(directory / "commands") == "WAKE_BEGIN\nWAKE_BEGIN\nWAKE_BEGIN\n"; });
    session.set_continuous(false);
    assert(!session.continuous());
    wait_until([&] {
        const auto events = session.poll();
        assert(!has_event(events, SessionEventKind::recording));
        assert(!has_event(events, SessionEventKind::assistant_message));
        return session.session_ready();
    });
    assert(read_file(directory / "commands") == "WAKE_BEGIN\nWAKE_BEGIN\nWAKE_BEGIN\nABORT\n");
}

void test_continuous_failure(const fs::path &directory)
{
    std::ofstream(directory / "scenario") << "capture-open";
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.set_continuous(true);
    wait_until([&] { return has_event(session.poll(), SessionEventKind::capture_error); });
    assert(!session.continuous());
    fs::remove(directory / "scenario");
    wait_until([&] {
        const auto events = session.poll();
        assert(!has_event(events, SessionEventKind::recording));
        return session.session_ready();
    });
    assert(read_file(directory / "commands") == "WAKE_BEGIN\n");
}

void test_recording_requires_capture_ack(const fs::path &directory)
{
    std::ofstream(directory / "scenario") << "delay-capture";
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.begin_push_to_talk();
    wait_until([&] {
        assert(!has_event(session.poll(), SessionEventKind::recording));
        return read_file(directory / "commands") == "PTT_BEGIN\n";
    });
    std::ofstream(directory / "capture-ready") << "yes";
    wait_until([&] { return has_event(session.poll(), SessionEventKind::recording); });
    session.end_push_to_talk();
    fs::remove(directory / "scenario");
}

void test_press_interrupts_response(const fs::path &directory, bool release_early)
{
    std::ofstream(directory / "scenario") << "hold-response";
    fs::remove(directory / "commands");
    SessionController session;
    session.start();
    wait_until([&] { (void)session.poll(); return session.session_ready(); });
    session.begin_push_to_talk();
    wait_until([&] { return has_event(session.poll(), SessionEventKind::recording); });
    session.end_push_to_talk();
    wait_until([&] { return has_event(session.poll(), SessionEventKind::thinking); });
    session.begin_push_to_talk();
    assert(!session.session_ready());
    if (release_early) session.end_push_to_talk();
    bool recording = false;
    wait_until([&] {
        const auto events = session.poll();
        assert(!has_event(events, SessionEventKind::assistant_message));
        recording = recording || has_event(events, SessionEventKind::recording);
        return release_early ? session.session_ready() : recording;
    });
    assert(recording != release_early);
    assert(read_file(directory / "commands") ==
           (release_early ? "PTT_BEGIN\nPTT_END\nABORT\n" : "PTT_BEGIN\nPTT_END\nABORT\nPTT_BEGIN\n"));
    session.interrupt();
    fs::remove(directory / "scenario");
}

void test_real_helper_chat_log_format(const fs::path &directory)
{
    std::ofstream(directory / "scenario") << "chat-events";
    AssistantViewModel view_model;
    view_model.start();
    wait_state(view_model, AssistantState::listening);
    view_model.begin_push_to_talk();
    wait_state(view_model, AssistantState::recording);
    view_model.end_push_to_talk();
    wait_until([&] {
        view_model.tick();
        return std::string(lv_subject_get_string(view_model.subtitle_subject())) == "今天天气怎么样？";
    });
    assert(std::string(lv_subject_get_string(view_model.speaker_subject())) == "You said");
    assert(view_model.messages().back().role == MessageRole::user);
    assert(view_model.messages().back().text == "今天天气怎么样？");

    std::ofstream(directory / "assistant-reply") << "yes";
    wait_until([&] {
        view_model.tick();
        return view_model.emotion() == "happy" && view_model.state() == AssistantState::listening &&
               std::string(lv_subject_get_string(view_model.subtitle_subject())) == "今天适合出门散步。";
    });
    assert(std::string(lv_subject_get_string(view_model.speaker_subject())) == "Xiaozhi");
    assert(view_model.messages().back().role == MessageRole::assistant);
    assert(view_model.messages().back().text == "今天适合出门散步。");
    std::ofstream(directory / "legacy-emotion") << "yes";
    wait_until([&] { view_model.tick(); return view_model.emotion() == "sad"; });
    assert(std::string(lv_subject_get_string(view_model.subtitle_subject())) == "今天适合出门散步。");
    fs::remove(directory / "scenario");
}

void test_chat_mentions_are_not_control(const fs::path &directory)
{
    fs::remove(directory / "scenario");
    fs::remove(directory / "commands");
    const auto saved_config = read_file(directory / "connection.conf");
    AssistantViewModel view_model;
    view_model.start();
    wait_state(view_model, AssistantState::listening);
    view_model.set_continuous(true);
    wait_state(view_model, AssistantState::recording);
    std::ofstream(directory / "control-looking-reply") << "yes";
    wait_until([&] {
        view_model.tick();
        return view_model.messages().back().text == "日志内容检查完成";
    });
    assert(view_model.continuous());
    assert(view_model.state() == AssistantState::speaking);
    assert(view_model.emotion() == "neutral");
    assert(std::string(lv_subject_get_string(view_model.activation_subject())).empty());
    assert(std::string(lv_subject_get_string(view_model.subtitle_subject())) == "日志内容检查完成");
    assert(read_file(directory / "commands") == "WAKE_BEGIN\n");
    assert(read_file(directory / "connection.conf") == saved_config);

    // The new helper validates its URL/token environment: ignored diagnostic
    // CONFIG strings must not poison the next connection in memory either.
    view_model.new_conversation();
    wait_state(view_model, AssistantState::listening);
    assert(!view_model.continuous());
    assert(read_file(directory / "connection.conf") == saved_config);
}

void assert_uuid_v4(const std::string &uuid)
{
    assert(uuid.size() == 36);
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) assert(uuid[i] == '-');
        else assert(std::string("0123456789abcdef").find(uuid[i]) != std::string::npos);
    }
    assert(uuid[14] == '4');
    assert(std::string("89ab").find(uuid[19]) != std::string::npos);
}

void test_linux_identity_persistence(const fs::path &directory)
{
    const auto root = directory / "identity-system";
    const auto path = directory / "native-device.conf";
    fs::create_directories(root / "sys/class/net/eth0");
    fs::create_directories(root / "sys/class/net/wlan0");
    fs::create_directories(root / "etc");
    std::ofstream(root / "sys/class/net/eth0/address") << "66:f9:cd:ea:3d:6e\n";
    std::ofstream(root / "sys/class/net/wlan0/address") << "02:11:22:33:44:55\n";
    std::ofstream(root / "etc/machine-id") << "0123456789ABCDEF0123456789ABCDEF\n";
    std::string device;
    std::string client;
    const auto load = [&] {
        return xiaozhi::detail::load_session_identity(path.string(), true, device, client, root.string());
    };
    assert(load());
    assert(device == "66:f9:cd:ea:3d:6e");
    assert(client == "01234567-89ab-4def-8123-456789abcdef");
    const auto saved = read_file(path);
    struct stat info{};
    assert(stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);

    // USB Ethernet may choose a new MAC on every boot. Saved pairing IDs must survive it.
    std::ofstream(root / "sys/class/net/eth0/address") << "ba:98:76:54:32:10\n";
    std::ofstream(root / "etc/machine-id") << "uninitialized\n";
    device.clear();
    client.clear();
    assert(load());
    assert(device + "\n" + client + "\n" == saved);

    setenv("XIAOZHI_DEVICE_ID", "02:aa:bb:cc:dd:ee", 1);
    assert(load());
    assert(device == "02:aa:bb:cc:dd:ee");
    assert(client == "01234567-89ab-4def-8123-456789abcdef");
    setenv("XIAOZHI_CLIENT_ID", "11111111-2222-4333-8444-555555555555", 1);
    assert(load());
    assert(client == "11111111-2222-4333-8444-555555555555");
    assert(read_file(path) == saved);
    unsetenv("XIAOZHI_DEVICE_ID");
    unsetenv("XIAOZHI_CLIENT_ID");
    assert(load());
    assert(device + "\n" + client + "\n" == saved);

    fs::remove(path);
    fs::remove(root / "sys/class/net/eth0/address");
    assert(load());
    assert(device == "02:11:22:33:44:55");
    assert_uuid_v4(client);
    const auto fallback_identity = read_file(path);
    fs::remove(root / "sys/class/net/wlan0/address");
    fs::remove(root / "etc/machine-id");
    assert(load());
    assert(device + "\n" + client + "\n" == fallback_identity);

    fs::remove(path);
    assert(load());
    assert(device.size() == 17 && device.rfind("02:", 0) == 0);
    assert_uuid_v4(client);
    const auto generated_identity = read_file(path);
    assert(load());
    assert(device + "\n" + client + "\n" == generated_identity);

    std::ofstream(path) << "incomplete-device\n";
    assert(!load());
    assert(read_file(path) == "incomplete-device\n");
}

void test_retry_requires_saved_identity(const fs::path &directory)
{
    const auto data = directory / "damaged-identity";
    fs::create_directory(data);
    std::ofstream(data / "device.conf") << "02:11:22:33:44:55\n";
    std::ofstream(data / "connection.conf") << "url=wss://example.invalid/xiaozhi/\ntoken=test-token\n";
    setenv("LILYGO_XIAOZHI_DATA_DIR", data.c_str(), 1);
    {
        AssistantViewModel view_model;
        view_model.start();
        assert(view_model.state() == AssistantState::error);
        assert(has_notice(view_model, "Cannot read or save the device identity"));
        assert(!fs::exists(data / "helper-starts"));
        view_model.retry();
        assert(view_model.state() == AssistantState::error);
        assert(std::string(lv_subject_get_string(view_model.detail_subject())) ==
               "Cannot read or save the device identity");
        view_model.tick();
        assert(!fs::exists(data / "helper-starts"));

        // Retry must reload the repaired identity before allowing a helper to start.
        std::ofstream(data / "device.conf") <<
            "02:11:22:33:44:55\n01234567-89ab-4def-8123-456789abcdef\n";
        view_model.retry();
        wait_state(view_model, AssistantState::listening);
        assert(read_file(data / "helper-starts") == "session\n");
    }
    setenv("LILYGO_XIAOZHI_DATA_DIR", directory.c_str(), 1);
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1) return helper(argv[1]);
    const auto executable = fs::absolute(argv[0]).string();
    char temporary[]      = "/tmp/xiaozhi-session-XXXXXX";
    assert(mkdtemp(temporary));
    const fs::path directory = temporary;
    unsetenv("LILYGO_XIAOZHI_MOCK");
    setenv("LILYGO_XIAOZHI_DATA_DIR", temporary, 1);
    setenv("LILYGO_XIAOZHI_HELPER", executable.c_str(), 1);
    unsetenv("XIAOZHI_DEVICE_ID");
    unsetenv("XIAOZHI_CLIENT_ID");
    assert(chdir("/tmp") == 0);
    test_linux_identity_persistence(directory);
    lv_init();
    test_retry_requires_saved_identity(directory);
    std::string identity;
    {
        AssistantViewModel view_model;
        view_model.start();
        assert(view_model.state() != AssistantState::listening);
        wait_state(view_model, AssistantState::binding);
        assert(std::string(lv_subject_get_string(view_model.activation_subject())) == "123456");
        assert(!fs::exists(directory / "connection.conf"));
        identity = read_file(directory / "device.conf");
        assert(!identity.empty());
        std::ofstream(directory / "allow-bind") << "yes";
        wait_state(view_model, AssistantState::listening);
        assert(std::string(lv_subject_get_string(view_model.activation_subject())).empty());
        assert(read_file(directory / "connection.conf") == "url=wss://example.invalid/xiaozhi/\ntoken=test-token\n");
        struct stat info{};
        assert(stat((directory / "connection.conf").c_str(), &info) == 0);
        assert((info.st_mode & 0777) == 0600);
        assert(stat(temporary, &info) == 0 && (info.st_mode & 0777) == 0700);
    }
    {
        AssistantViewModel view_model;
        view_model.start();
        wait_state(view_model, AssistantState::listening);
        assert(read_file(directory / "device.conf") == identity);
        assert(read_file(directory / "activation-count") == "activate\n");
    }
    for (const auto *stage : {"open", "warmup", "read"}) test_capture_failure(directory, stage);
    test_pending_recording(directory);
    for (const auto *scenario : {"disconnect", "disconnect-before-ready", "legacy-error"})
        test_reconnect_clears_press(directory, scenario);
    test_visible_capture_failure(directory);
    test_continuous_rounds_and_stop(directory);
    test_continuous_failure(directory);
    test_recording_requires_capture_ack(directory);
    test_press_interrupts_response(directory, false);
    test_press_interrupts_response(directory, true);
    test_real_helper_chat_log_format(directory);
    test_chat_mentions_are_not_control(directory);
    fs::remove(directory / "connection.conf");
    std::ofstream(directory / "fail-activation") << "yes";
    {
        AssistantViewModel view_model;
        view_model.start();
        wait_state(view_model, AssistantState::reconnecting);
        assert(!fs::exists(directory / "connection.conf"));
    }
    lv_deinit();
    fs::remove_all(directory);
}

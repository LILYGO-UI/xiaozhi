#ifndef LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_SESSION_CONTROLLER_HPP
#define LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_SESSION_CONTROLLER_HPP

#include <chrono>
#include <string>
#include <vector>

enum class SessionEventKind {
    binding_code,
    connecting,
    ready,
    recording,
    thinking,
    speaking,
    response_done,
    continuous_changed,
    reconnecting,
    user_message,
    assistant_message,
    emotion,
    wake_detected,
    unavailable,
    capture_error,
    error,
};

struct SessionEvent {
    SessionEventKind kind;
    std::string text;
};

namespace xiaozhi::detail {

bool load_session_identity(const std::string &path, bool linux_identity,
                           std::string &device_id, std::string &client_id,
                           const std::string &system_root = "/");

}

class SessionController {
public:
    SessionController();
    ~SessionController();

    SessionController(const SessionController &)            = delete;
    SessionController &operator=(const SessionController &) = delete;

    void start();
    void stop() noexcept;
    void retry();
    void begin_push_to_talk();
    void end_push_to_talk();
    void new_conversation();
    void set_continuous(bool enabled);
    [[nodiscard]] bool continuous() const noexcept;
    void interrupt();
    [[nodiscard]] std::vector<SessionEvent> poll();
    [[nodiscard]] bool session_ready() const noexcept;
    [[nodiscard]] bool wake_word_available() const noexcept;

private:
    enum class ChildMode { none, activation, session };

    void queue(SessionEventKind kind, std::string text = {});
    void load_config();
    void save_config();
    bool load_identity();
    void resolve_helpers();
    bool spawn_activation();
    bool spawn_session();
    bool spawn_child(const std::vector<std::string> &arguments, ChildMode mode);
    void stop_child() noexcept;
    void read_child_output();
    void parse_line(std::string line);
    void handle_child_exit();
    void fail_session(SessionEventKind kind, std::string text);
    void schedule_retry();
    bool write_control(const char *command);
    void start_wake_word();
    void stop_wake_word() noexcept;
    void read_wake_word_output();
    void begin_continuous_turn();
    void disable_continuous();
    void abort_turn();

    std::string data_directory_;
    std::string config_path_;
    std::string control_path_;
    std::string helper_path_;
    std::string kws_helper_path_;
    std::string kws_model_path_;
    std::string server_url_;
    std::string token_;
    std::string device_id_;
    std::string client_id_;
    std::string read_buffer_;
    std::string kws_read_buffer_;
    std::vector<SessionEvent> pending_events_;
    ChildMode child_mode_   = ChildMode::none;
    int child_pid_          = -1;
    int child_output_fd_    = -1;
    int control_fd_         = -1;
    int kws_pid_            = -1;
    int kws_output_fd_      = -1;
    int reconnect_attempts_ = 0;
    bool started_           = false;
    bool ready_             = false;
    bool ptt_active_        = false;
    bool ptt_pending_       = false;
    bool session_failed_    = false;
    bool unavailable_sent_  = false;
    bool mock_              = false;
    bool continuous_        = false;
    bool turn_active_       = false;
    bool aborting_          = false;
    bool wake_suspended_    = false;
    bool next_continuous_turn_ = false;
    bool response_done_reported_ = false;
    std::chrono::steady_clock::time_point retry_at_{};
    std::chrono::steady_clock::time_point abort_deadline_{};
};

#endif

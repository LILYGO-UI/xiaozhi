#include "pages/assistant/session_controller.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr const char *default_url = "wss://api.tenclass.net:443/xiaozhi/v1/";

std::string environment_value(const char *name)
{
    const auto *value = std::getenv(name);
    return value && value[0] ? value : "";
}

bool executable(const std::string &path)
{
    return !path.empty() && access(path.c_str(), X_OK) == 0;
}

bool readable(const std::string &path)
{
    return !path.empty() && access(path.c_str(), R_OK) == 0;
}

std::string first_available(const std::vector<std::string> &paths, bool require_executable)
{
    const auto found = std::find_if(paths.begin(), paths.end(), [require_executable](const auto &path) {
        return require_executable ? executable(path) : readable(path);
    });
    return found == paths.end() ? std::string{} : *found;
}

std::string trim(std::string value)
{
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t'))
        value.pop_back();
    const auto first = value.find_first_not_of(" \t");
    return first == std::string::npos ? std::string{} : value.substr(first);
}

std::string value_after(const std::string &line, std::string_view marker)
{
    const auto offset = line.find(marker);
    return offset == std::string::npos ? std::string{} : trim(line.substr(offset + marker.size()));
}

bool write_private_file(const std::string &path, const std::string &contents)
{
    std::string temporary = path + ".XXXXXX";
    const int fd          = mkstemp(temporary.data());  // Created with 0600 before writing credentials.
    if (fd < 0) return false;
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const auto count = write(fd, contents.data() + offset, contents.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
    }
    const bool flushed = fsync(fd) == 0;
    const bool closed  = close(fd) == 0;
    if (offset == contents.size() && flushed && closed && rename(temporary.c_str(), path.c_str()) == 0) return true;
    unlink(temporary.c_str());
    return false;
}

void close_fd(int &fd)
{
    if (fd >= 0) close(fd);
    fd = -1;
}

void terminate_process(int &pid)
{
    if (pid <= 0) return;
    kill(pid, SIGTERM);
    for (int attempt = 0; attempt < 10; ++attempt) {
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) {
            pid = -1;
            return;
        }
        usleep(20000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    pid = -1;
}

}  // namespace

SessionController::SessionController()
{
#if defined(CM0_APP_SIMULATOR)
    mock_ = environment_value("LILYGO_XIAOZHI_MOCK") == "1";
#endif
    auto base = environment_value("XDG_DATA_HOME");
    if (base.empty()) {
        const auto home = environment_value("HOME");
        base            = home.empty() ? "/tmp" : home + "/.local/share";
    }
    data_directory_ = base + "/lilygo-ui-xiaozhi";
#if defined(CM0_APP_SIMULATOR)
    data_directory_ = LILYGO_XIAOZHI_PROJECT_DATA_DIR;
#endif
    const auto override_directory = environment_value("LILYGO_XIAOZHI_DATA_DIR");
    if (!override_directory.empty()) data_directory_ = override_directory;
    config_path_  = data_directory_ + "/connection.conf";
    control_path_ = data_directory_ + "/session.ctl";
}

SessionController::~SessionController()
{
    stop();
}

void SessionController::start()
{
    if (started_) return;
    started_ = true;
    if (mock_) {
        ready_ = true;
        queue(SessionEventKind::ready, "Simulator session ready");
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(data_directory_, error);
    if (error || chmod(data_directory_.c_str(), 0700) != 0) {
        disable_continuous();
        queue(SessionEventKind::error, "Cannot create or access the pairing data folder");
        return;
    }
    if (!load_identity()) {
        disable_continuous();
        queue(SessionEventKind::error, "Cannot read or save the device identity");
        return;
    }
    resolve_helpers();
    load_config();
    if (helper_path_.empty()) {
        disable_continuous();
        unavailable_sent_ = true;
        queue(SessionEventKind::unavailable, "Xiaozhi protocol helper not found");
        return;
    }
    if (token_.empty())
        spawn_activation();
    else
        spawn_session();
}

void SessionController::stop() noexcept
{
    if (!started_) return;
    disable_continuous();
    write_control("QUIT");
    stop_wake_word();
    stop_child();
    unlink(control_path_.c_str());
    started_ = false;
    ready_   = false;
}

void SessionController::retry()
{
    if (mock_) return;
    disable_continuous();
    wake_suspended_ = true;
    stop_wake_word();
    stop_child();
    reconnect_attempts_ = 0;
    unavailable_sent_   = false;
    if (!load_identity()) {
        queue(SessionEventKind::error, "Cannot read or save the device identity");
        return;
    }
    resolve_helpers();
    load_config();
    if (helper_path_.empty()) {
        queue(SessionEventKind::unavailable, "Xiaozhi protocol helper not found");
        unavailable_sent_ = true;
    } else if (token_.empty()) {
        spawn_activation();
    } else {
        spawn_session();
    }
}

void SessionController::begin_push_to_talk()
{
    const bool was_continuous = continuous_;
    disable_continuous();
    if (mock_) {
        ptt_active_ = true;
        turn_active_ = true;
        ptt_pending_ = false;
        queue(SessionEventKind::recording, "Listening");
        return;
    }
    if (turn_active_ && (!ptt_active_ || was_continuous)) {
        abort_turn();
        ptt_pending_ = true;
        return;
    }
    if (ptt_active_) return;
    if (!ready_) {
        ptt_pending_ = true;
        queue(SessionEventKind::connecting, "Recording will start once connected");
        return;
    }
    stop_wake_word();
    if (write_control("PTT_BEGIN")) {
        ptt_active_ = true;
        turn_active_ = true;
        ptt_pending_ = false;
        response_done_reported_ = false;
        queue(SessionEventKind::connecting, "Preparing microphone");
    } else {
        fail_session(SessionEventKind::error, "Cannot send the recording command. Reconnecting automatically.");
    }
}

void SessionController::end_push_to_talk()
{
    ptt_pending_ = false;
    if (mock_) {
        if (!ptt_active_) return;
        ptt_active_ = false;
        turn_active_ = false;
        queue(SessionEventKind::thinking, "Processing");
        queue(SessionEventKind::user_message, "What is the weather like today?");
        queue(SessionEventKind::assistant_message, "It is a good day to go out. Dress for the current weather.");
        queue(SessionEventKind::emotion, "happy");
        queue(SessionEventKind::response_done, "Reply complete");
        return;
    }
    if (!ptt_active_) return;
    if (!write_control("PTT_END")) {
        fail_session(SessionEventKind::error, "Cannot finish recording. Reconnecting automatically.");
        return;
    }
    ptt_active_ = false;
    queue(SessionEventKind::thinking, "Waiting for a reply");
}

void SessionController::new_conversation()
{
    disable_continuous();
    wake_suspended_ = true;
    if (mock_) {
        ptt_active_ = false;
        ptt_pending_ = false;
        turn_active_ = false;
        queue(SessionEventKind::ready, "New conversation ready");
        return;
    }
    ptt_active_  = false;
    ptt_pending_ = false;
    stop_wake_word();
    stop_child();
    reconnect_attempts_ = 0;
    if (token_.empty())
        spawn_activation();
    else
        spawn_session();
}

void SessionController::disable_continuous()
{
    next_continuous_turn_ = false;
    if (!continuous_) return;
    continuous_ = false;
    queue(SessionEventKind::continuous_changed, "0");
}

bool SessionController::continuous() const noexcept { return continuous_; }

void SessionController::set_continuous(bool enabled)
{
    if (enabled == continuous_) return;
    if (!enabled) {
        interrupt();
        return;
    }
    if (started_ && !mock_ && helper_path_.empty()) {
        queue(SessionEventKind::unavailable, "Xiaozhi protocol helper not found");
        return;
    }
    continuous_ = true;
    wake_suspended_ = true;
    queue(SessionEventKind::continuous_changed, "1");
    stop_wake_word();
    // Enable explicitly while connecting, but never carry the setting through a failure.
    if (ready_ && !turn_active_ && !aborting_) begin_continuous_turn();
}

void SessionController::begin_continuous_turn()
{
    next_continuous_turn_ = false;
    if (!continuous_ || !ready_ || turn_active_ || aborting_) return;
    stop_wake_word();
    if (mock_ || write_control("WAKE_BEGIN")) {
        turn_active_ = true;
        ptt_active_ = true;
        response_done_reported_ = false;
        if (mock_) queue(SessionEventKind::recording, "Continuous conversation: listening");
    } else {
        fail_session(SessionEventKind::error, "Cannot start continuous conversation. Reconnecting automatically.");
    }
}

void SessionController::abort_turn()
{
    stop_wake_word();
    wake_suspended_ = true;
    ptt_active_ = false;
    ptt_pending_ = false;
    turn_active_ = false;
    next_continuous_turn_ = false;
    if (mock_) {
        queue(SessionEventKind::response_done, "Stopped");
        return;
    }
    if (aborting_) return;
    if (child_mode_ != ChildMode::session || child_pid_ <= 0) return;
    aborting_ = true;
    ready_ = false;
    abort_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (!write_control("ABORT")) abort_deadline_ = std::chrono::steady_clock::now();
    queue(SessionEventKind::connecting, "Stopped. Preparing a new conversation.");
}

void SessionController::interrupt()
{
    disable_continuous();
    abort_turn();
}

std::vector<SessionEvent> SessionController::poll()
{
    if (!started_) start();
    if (!mock_) {
        read_child_output();
        read_wake_word_output();
        handle_child_exit();
        if (aborting_ && std::chrono::steady_clock::now() >= abort_deadline_) {
            const bool pending_press = ptt_pending_;
            stop_child();
            spawn_session();
            ptt_pending_ = pending_press;
        }
        // Older helpers can stay alive after a failed turn. Their server-side
        // listen state is no longer reliable, so always establish a fresh session.
        if (session_failed_ && child_mode_ == ChildMode::session) {
            stop_child();
            schedule_retry();
        }
        if (next_continuous_turn_) begin_continuous_turn();
        if (child_pid_ <= 0 && retry_at_ != std::chrono::steady_clock::time_point{} &&
            std::chrono::steady_clock::now() >= retry_at_) {
            retry_at_ = {};
            if (token_.empty())
                spawn_activation();
            else
                spawn_session();
        }
    }
    auto events = std::move(pending_events_);
    pending_events_.clear();
    return events;
}

bool SessionController::session_ready() const noexcept
{
    return ready_;
}

bool SessionController::wake_word_available() const noexcept
{
    return mock_ || (!kws_helper_path_.empty() && !kws_model_path_.empty());
}

void SessionController::queue(SessionEventKind kind, std::string text)
{
    pending_events_.push_back(SessionEvent{kind, std::move(text)});
}

void SessionController::resolve_helpers()
{
    helper_path_ =
        first_available({environment_value("LILYGO_XIAOZHI_HELPER"),
#if defined(CM0_APP_SIMULATOR)
                         LILYGO_XIAOZHI_HOST_HELPER,
#endif
                         "/usr/libexec/lilygo-ui-xiaozhi-helper", "/root/app/k230_phone_ui/k230_xiaozhi_probe"},
                        true);
    kws_helper_path_ =
        first_available({environment_value("LILYGO_XIAOZHI_KWS_HELPER"), "/usr/libexec/lilygo-ui-xiaozhi-kws",
                         "/root/app/k230_phone_ui/k230_xiaozhi_kws"},
                        true);
    kws_model_path_ = first_available(
        {environment_value("LILYGO_XIAOZHI_KWS_MODEL"), "/usr/share/lilygo-ui-xiaozhi/xiaozhi_kws.kmodel",
         "/root/app/k230_phone_ui/models/xiaozhi_kws.kmodel"},
        false);
}

void SessionController::load_config()
{
    server_url_ = default_url;
    token_.clear();
    std::ifstream input(config_path_);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("url=", 0) == 0 && line.size() > 4) server_url_ = trim(line.substr(4));
        if (line.rfind("token=", 0) == 0 && line.size() > 6) token_ = trim(line.substr(6));
    }
}

void SessionController::save_config()
{
    if (server_url_.empty() || token_.empty()) return;
    if (!write_private_file(config_path_, "url=" + server_url_ + "\ntoken=" + token_ + "\n"))
        queue(SessionEventKind::error, "Cannot save the pairing token. Pair again on the next launch.");
}

bool SessionController::load_identity()
{
    return xiaozhi::detail::load_session_identity(data_directory_ + "/device.conf",
#if defined(CM0_APP_SIMULATOR)
                                                 false,
#else
                                                 true,
#endif
                                                 device_id_, client_id_);
}

bool xiaozhi::detail::load_session_identity(const std::string &path, bool linux_identity,
                                           std::string &device_id, std::string &client_id,
                                           const std::string &system_root)
{
    std::ifstream input(path);
    if (input) {
        std::getline(input, device_id);
        std::getline(input, client_id);
        if (device_id.empty() || client_id.empty()) return false;
    } else {
        std::random_device random;
        constexpr char hex[] = "0123456789abcdef";
        auto random_hex = [&]() { return hex[random() & 15]; };
        device_id = "02";
        for (int i = 0; i < 5; ++i) {
            device_id += ':';
            device_id += random_hex();
            device_id += random_hex();
        }
        std::string machine_id;
        if (linux_identity) {
            const auto root = std::filesystem::path(system_root);
            // Preserve the helper's current identity during first-run migration.
            for (const auto *interface : {"eth0", "wlan0", "usb0"}) {
                std::ifstream address(root / "sys/class/net" / interface / "address");
                std::string value;
                if (std::getline(address, value) && !trim(value).empty()) {
                    device_id = trim(std::move(value));
                    break;
                }
            }
            std::ifstream machine_id_file(root / "etc/machine-id");
            std::getline(machine_id_file, machine_id);
            machine_id = trim(std::move(machine_id));
            for (auto &digit : machine_id)
                if (digit >= 'A' && digit <= 'F') digit += 'a' - 'A';
        }
        const bool valid_machine_id = machine_id.size() == 32 &&
            machine_id.find_first_not_of(hex) == std::string::npos;
        client_id.clear();
        for (int i = 0; i < 32; ++i) {
            if (i == 8 || i == 12 || i == 16 || i == 20) client_id += '-';
            const char digit = valid_machine_id ? machine_id[i] : random_hex();
            const auto value = digit >= 'a' ? digit - 'a' + 10 : digit - '0';
            client_id += i == 12 ? '4' : (i == 16 ? hex[8 + (value & 3)] : digit);
        }
        if (!write_private_file(path, device_id + "\n" + client_id + "\n")) return false;
    }
    // Explicit native overrides affect this run without replacing the saved identity.
    if (linux_identity) {
        const auto explicit_device = environment_value("XIAOZHI_DEVICE_ID");
        const auto explicit_client = environment_value("XIAOZHI_CLIENT_ID");
        if (!explicit_device.empty()) device_id = explicit_device;
        if (!explicit_client.empty()) client_id = explicit_client;
    }
    return true;
}

bool SessionController::spawn_activation()
{
    ready_ = false;
    token_.clear();
    queue(SessionEventKind::connecting, "Getting device pairing information");
    return spawn_child({helper_path_, "activate", "--activate-timeout", "86400", "--tls-verify"},
                       ChildMode::activation);
}

bool SessionController::spawn_session()
{
    if (helper_path_.empty() || token_.empty()) return false;
    unlink(control_path_.c_str());
    if (mkfifo(control_path_.c_str(), 0600) != 0 && errno != EEXIST) {
        queue(SessionEventKind::error, "Cannot create the session control channel");
        schedule_retry();
        return false;
    }
    control_fd_ = open(control_path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (control_fd_ < 0) {
        queue(SessionEventKind::error, "Cannot open the session control channel");
        schedule_retry();
        return false;
    }
    ready_ = false;
    queue(SessionEventKind::connecting, "Connecting to Xiaozhi");
    return spawn_child({helper_path_, "session", "--control", control_path_, "--seconds", "30", "--wait", "120",
                        "--timeout-ms", "10000", "--tls-verify"},
                       ChildMode::session);
}

bool SessionController::spawn_child(const std::vector<std::string> &arguments, ChildMode mode)
{
    int output_pipe[2] = {-1, -1};
    if (arguments.empty() || pipe(output_pipe) != 0) {
        queue(SessionEventKind::error, "Cannot create the helper output channel");
        schedule_retry();
        return false;
    }
    const auto pid = fork();
    if (pid == 0) {
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (!server_url_.empty()) setenv("XIAOZHI_URL", server_url_.c_str(), 1);
        setenv("XIAOZHI_TOKEN", token_.c_str(), 1);
        if (!device_id_.empty()) setenv("XIAOZHI_DEVICE_ID", device_id_.c_str(), 1);
        if (!client_id_.empty()) setenv("XIAOZHI_CLIENT_ID", client_id_.c_str(), 1);
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto &argument : arguments) argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        execv(argv.front(), argv.data());
        _exit(127);
    }
    close(output_pipe[1]);
    if (pid < 0) {
        close(output_pipe[0]);
        queue(SessionEventKind::error, "Cannot start the Xiaozhi helper");
        schedule_retry();
        return false;
    }
    fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    child_pid_       = pid;
    child_output_fd_ = output_pipe[0];
    child_mode_      = mode;
    session_failed_  = false;
    read_buffer_.clear();
    return true;
}

void SessionController::stop_child() noexcept
{
    terminate_process(child_pid_);
    close_fd(child_output_fd_);
    close_fd(control_fd_);
    child_mode_ = ChildMode::none;
    ready_      = false;
    ptt_active_ = false;
    ptt_pending_ = false;
    session_failed_ = false;
    turn_active_ = false;
    aborting_ = false;
    next_continuous_turn_ = false;
    retry_at_ = {};
}

void SessionController::read_child_output()
{
    if (child_output_fd_ < 0) return;
    char buffer[2048];
    for (;;) {
        const auto count = read(child_output_fd_, buffer, sizeof(buffer));
        if (count > 0) {
            read_buffer_.append(buffer, static_cast<std::size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    std::size_t newline = 0;
    while ((newline = read_buffer_.find('\n')) != std::string::npos) {
        parse_line(read_buffer_.substr(0, newline));
        read_buffer_.erase(0, newline + 1);
    }
    constexpr std::size_t maximum_buffer = 16384;
    if (read_buffer_.size() > maximum_buffer) read_buffer_.erase(0, read_buffer_.size() - maximum_buffer);
}

void SessionController::parse_line(std::string line)
{
    auto message = trim(std::move(line));
    if (message.empty() || aborting_ || session_failed_) return;
    // Strip only the helper's optional timestamp and known log level. Diagnostic
    // JSON (INFO text: ...) and chat content must never become control messages.
    if (message.front() == '[') {
        const auto timestamp_end = message.find("] ");
        if (timestamp_end != std::string::npos) message = trim(message.substr(timestamp_end + 2));
    }
    std::string level;
    const auto separator = message.find_first_of(" \t");
    if (separator != std::string::npos) {
        const auto candidate = message.substr(0, separator);
        if (candidate == "INFO" || candidate == "WARN" || candidate == "ERR" || candidate == "EVENT" ||
            candidate == "CHAT" || candidate == "EMOTION") {
            level = candidate;
            message = trim(message.substr(separator + 1));
        }
    }
    const auto starts_with = [&](std::string_view prefix) { return message.rfind(prefix, 0) == 0; };
    const bool info = level.empty() || level == "INFO";
    const bool error = level.empty() || level == "WARN" || level == "ERR";

    if (level == "CHAT") {
        if (starts_with("user:")) queue(SessionEventKind::user_message, trim(message.substr(5)));
        else if (starts_with("assistant:")) queue(SessionEventKind::assistant_message, trim(message.substr(10)));
        return;
    }
    if (level == "EMOTION" || (level.empty() && starts_with("EMOTION:"))) {
        queue(SessionEventKind::emotion, level.empty() ? trim(message.substr(8)) : message);
        return;
    }
    if (level.empty() && child_mode_ == ChildMode::activation && starts_with("CONFIG websocket.url=")) {
        server_url_ = value_after(message, "CONFIG websocket.url=");
    } else if (level.empty() && child_mode_ == ChildMode::activation && starts_with("CONFIG websocket.token=")) {
        token_ = value_after(message, "CONFIG websocket.token=");
    } else if (info && child_mode_ == ChildMode::activation && starts_with("ACTIVATION_CODE:")) {
        queue(SessionEventKind::binding_code, value_after(message, "ACTIVATION_CODE:"));
    } else if (info && child_mode_ == ChildMode::activation && starts_with("ACTIVATION_DONE ")) {
        queue(SessionEventKind::connecting, "Device paired. Starting a conversation.");
    } else if (info && child_mode_ == ChildMode::session &&
               (message == "session ready" || starts_with("session ready:"))) {
        ready_              = true;
        reconnect_attempts_ = 0;
        queue(SessionEventKind::ready, wake_word_available() ? "Say \"Xiaozhi Xiaozhi\" to wake" : "Hold the button to talk");
        start_wake_word();
        if (ptt_pending_) begin_push_to_talk();
        else if (continuous_) next_continuous_turn_ = true;
    } else if (level == "EVENT" && starts_with("capture_error stage=")) {
        const auto stage = value_after(message, "stage=");
        const auto reason = stage.rfind("open", 0) == 0 ? "cannot open the microphone"
                          : stage.rfind("warmup", 0) == 0 ? "microphone is not ready"
                                                         : "cannot read from the microphone";
        fail_session(SessionEventKind::capture_error,
                     std::string("Recording failed: ") + reason + ". Check microphone permissions and the input device, then retry.");
    } else if (level == "EVENT" && starts_with("playback_error stage=")) {
        fail_session(SessionEventKind::error, "Playback failed. Check the speaker and audio output device, then retry.");
    } else if (level == "EVENT" && message == "capture_started") {
        if (turn_active_ && ptt_active_)
            queue(SessionEventKind::recording, continuous_ ? "Continuous conversation: listening" : "Release to send");
    } else if (level == "EVENT" && message == "audio_start") {
        queue(SessionEventKind::speaking, "Playing reply");
    } else if (level == "EVENT" && message == "request_sent") {
        ptt_active_ = false;
        queue(SessionEventKind::thinking, "Thinking");
    } else if (level == "EVENT" && message == "response_done") {
        if (turn_active_ && !response_done_reported_) {
            response_done_reported_ = true;
            queue(SessionEventKind::response_done, "Reply complete");
        }
    } else if ((info || error) &&
               (starts_with("turn done rc=") || starts_with("ptt turn done rc=") || starts_with("wake turn done rc="))) {
        const auto result_text = value_after(message, "turn done rc=");
        char *end = nullptr;
        const auto result = std::strtol(result_text.c_str(), &end, 10);
        if (result_text.empty() || !end || *end != '\0') return;
        if (result != 0) {
            fail_session(SessionEventKind::error, "Session interrupted. Reconnecting automatically.");
        } else if (turn_active_) {
            ptt_active_ = false;
            turn_active_ = false;
            if (!response_done_reported_) queue(SessionEventKind::response_done, "Reply complete");
            next_continuous_turn_ = continuous_;
            if (!continuous_) start_wake_word();
        }
    } else if (error && (message == "OTA check request failed" || starts_with("OTA check status="))) {
        disable_continuous();
        wake_suspended_ = true;
        queue(SessionEventKind::error, "Cannot get pairing information. Check the network and retry.");
    } else if (error && message == "session connect failed") {
        fail_session(SessionEventKind::error, "Session interrupted. Reconnecting automatically.");
    }
}

void SessionController::handle_child_exit()
{
    if (child_pid_ <= 0) return;
    int status        = 0;
    const auto result = waitpid(child_pid_, &status, WNOHANG);
    if (result != child_pid_) return;
    read_child_output();
    close_fd(child_output_fd_);
    close_fd(control_fd_);
    const auto mode = child_mode_;
    const bool was_aborting = aborting_;
    const bool pending_press = ptt_pending_;
    aborting_ = false;
    child_mode_     = ChildMode::none;
    child_pid_      = -1;
    ready_          = false;
    if (mode == ChildMode::session) {
        ptt_active_ = false;
        ptt_pending_ = false;
    }
    stop_wake_word();
    if (!started_) return;
    turn_active_ = false;
    if (was_aborting) {
        spawn_session();
        ptt_pending_ = pending_press;
        return;
    }
    if (mode == ChildMode::activation && WIFEXITED(status) && WEXITSTATUS(status) == 0 && !token_.empty()) {
        save_config();
        reconnect_attempts_ = 0;
        spawn_session();
    } else {
        if (mode == ChildMode::activation) token_.clear();
        schedule_retry();
    }
}

void SessionController::fail_session(SessionEventKind kind, std::string text)
{
    if (session_failed_) return;
    session_failed_ = true;
    disable_continuous();
    wake_suspended_ = true;
    turn_active_ = false;
    ready_ = false;
    ptt_active_ = false;
    ptt_pending_ = false;
    stop_wake_word();
    queue(kind, std::move(text));
}

void SessionController::schedule_retry()
{
    disable_continuous();
    wake_suspended_ = true;
    turn_active_ = false;
    ready_             = false;
    ptt_active_        = false;
    ptt_pending_       = false;
    const auto seconds = std::min(20, 2 * ++reconnect_attempts_);
    retry_at_          = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    queue(SessionEventKind::reconnecting, "Reconnecting in " + std::to_string(seconds) + " seconds");
}

bool SessionController::write_control(const char *command)
{
    if (control_fd_ < 0 || !command) return false;
    const std::string line = std::string(command) + "\n";
    return write(control_fd_, line.data(), line.size()) == static_cast<ssize_t>(line.size());
}

void SessionController::start_wake_word()
{
    if (!ready_ || turn_active_ || continuous_ || wake_suspended_ || kws_pid_ > 0 || !wake_word_available() || mock_) return;
    int output_pipe[2] = {-1, -1};
    if (pipe(output_pipe) != 0) return;
    const auto pid = fork();
    if (pid == 0) {
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[0]);
        close(output_pipe[1]);
        execl(kws_helper_path_.c_str(), kws_helper_path_.c_str(), "--model", kws_model_path_.c_str(), "--threshold",
              "0.50", "--debug", "0", static_cast<char *>(nullptr));
        _exit(127);
    }
    close(output_pipe[1]);
    if (pid < 0) {
        close(output_pipe[0]);
        return;
    }
    fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    kws_pid_       = pid;
    kws_output_fd_ = output_pipe[0];
    kws_read_buffer_.clear();
}

void SessionController::stop_wake_word() noexcept
{
    terminate_process(kws_pid_);
    close_fd(kws_output_fd_);
    kws_read_buffer_.clear();
}

void SessionController::read_wake_word_output()
{
    if (kws_output_fd_ < 0) return;
    char buffer[512];
    for (;;) {
        const auto count = read(kws_output_fd_, buffer, sizeof(buffer));
        if (count > 0)
            kws_read_buffer_.append(buffer, static_cast<std::size_t>(count));
        else if (count < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    if (kws_read_buffer_.find("KWS_DETECTED") != std::string::npos) {
        queue(SessionEventKind::wake_detected, "Wake word detected");
        stop_wake_word();
        if (write_control("WAKE_BEGIN")) {
            ptt_active_ = true;
            turn_active_ = true;
            response_done_reported_ = false;
        } else {
            fail_session(SessionEventKind::error, "Cannot start recording. Reconnecting automatically.");
        }
        return;
    }
    int status = 0;
    if (kws_pid_ > 0 && waitpid(kws_pid_, &status, WNOHANG) == kws_pid_) {
        kws_pid_ = -1;
        close_fd(kws_output_fd_);
    }
    constexpr std::size_t maximum_buffer = 4096;
    if (kws_read_buffer_.size() > maximum_buffer) kws_read_buffer_.erase(0, kws_read_buffer_.size() - maximum_buffer);
}

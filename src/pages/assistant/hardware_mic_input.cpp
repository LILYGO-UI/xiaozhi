#include "pages/assistant/hardware_mic_input.hpp"

#include <cerrno>
#include <cstdlib>
#include <string>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/input.h>
#endif

HardwareMicInput::HardwareMicInput() = default;

HardwareMicInput::~HardwareMicInput()
{
    for (const auto descriptor : descriptors_) close(descriptor);
}

std::vector<bool> HardwareMicInput::poll()
{
    if (!discovered_) discover();
    std::vector<bool> states;
#if defined(__linux__)
    for (const auto descriptor : descriptors_) {
        input_event event{};
        while (read(descriptor, &event, sizeof(event)) == sizeof(event)) {
            if (event.type != EV_KEY || event.value == 2) continue;
            bool is_mic_key = event.code == KEY_RECORD || event.code == KEY_MICMUTE;
#ifdef KEY_VOICECOMMAND
            is_mic_key = is_mic_key || event.code == KEY_VOICECOMMAND;
#endif
            if (is_mic_key) states.push_back(event.value != 0);
        }
    }
#endif
    return states;
}

void HardwareMicInput::discover()
{
    discovered_ = true;
#if defined(__linux__)
    if (const auto *configured = std::getenv("LILYGO_XIAOZHI_MIC_INPUT"); configured && configured[0]) {
        const auto descriptor = open(configured, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor >= 0) descriptors_.push_back(descriptor);
        return;
    }
    auto *directory = opendir("/dev/input");
    if (!directory) return;
    while (const auto *entry = readdir(directory)) {
        if (std::string(entry->d_name).rfind("event", 0) != 0) continue;
        const auto path       = std::string("/dev/input/") + entry->d_name;
        const auto descriptor = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor >= 0) descriptors_.push_back(descriptor);
    }
    closedir(directory);
#endif
}

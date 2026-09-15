#ifndef LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_HARDWARE_MIC_INPUT_HPP
#define LILYGO_UI_XIAOZHI_PAGES_ASSISTANT_HARDWARE_MIC_INPUT_HPP

#include <vector>

class HardwareMicInput {
public:
    HardwareMicInput();
    ~HardwareMicInput();

    HardwareMicInput(const HardwareMicInput &)            = delete;
    HardwareMicInput &operator=(const HardwareMicInput &) = delete;

    [[nodiscard]] std::vector<bool> poll();

private:
    void discover();

    std::vector<int> descriptors_;
    bool discovered_ = false;
};

#endif

#ifndef LILYGO_UI_TEMPLATE_LV_SUBJECT_HPP
#define LILYGO_UI_TEMPLATE_LV_SUBJECT_HPP

#include <lvgl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

class IntSubject {
public:
    explicit IntSubject(std::int32_t value, std::int32_t minimum = INT32_MIN, std::int32_t maximum = INT32_MAX)
    {
        lv_subject_init_int(&subject_, value);
        lv_subject_set_min_value_int(&subject_, minimum);
        lv_subject_set_max_value_int(&subject_, maximum);
    }

    ~IntSubject()
    {
        lv_subject_deinit(&subject_);
    }

    IntSubject(const IntSubject &)            = delete;
    IntSubject &operator=(const IntSubject &) = delete;
    IntSubject(IntSubject &&)                 = delete;
    IntSubject &operator=(IntSubject &&)      = delete;

    void set(std::int32_t value)
    {
        lv_subject_set_int(&subject_, value);
    }

    [[nodiscard]] std::int32_t value() const
    {
        return lv_subject_get_int(const_cast<lv_subject_t *>(&subject_));
    }

    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return &subject_;
    }

private:
    lv_subject_t subject_{};
};

template <std::size_t Capacity>
class StringSubject {
public:
    static_assert(Capacity > 1, "StringSubject needs room for a terminator");

    explicit StringSubject(std::string_view value = {})
    {
        assign(buffer_, value);
        previous_.fill('\0');
        lv_subject_init_string(&subject_, buffer_.data(), previous_.data(), buffer_.size(), buffer_.data());
    }

    ~StringSubject()
    {
        lv_subject_deinit(&subject_);
    }

    StringSubject(const StringSubject &)            = delete;
    StringSubject &operator=(const StringSubject &) = delete;
    StringSubject(StringSubject &&)                 = delete;
    StringSubject &operator=(StringSubject &&)      = delete;

    void set(std::string_view value)
    {
        std::array<char, Capacity> next{};
        assign(next, value);
        lv_subject_copy_string(&subject_, next.data());
    }

    [[nodiscard]] const char *value() const noexcept
    {
        return lv_subject_get_string(const_cast<lv_subject_t *>(&subject_));
    }

    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return &subject_;
    }

private:
    static void assign(std::array<char, Capacity> &destination, std::string_view value)
    {
        auto count = value.size() < Capacity - 1 ? value.size() : Capacity - 1;
        if (count < value.size()) {
            while (count > 0 && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80) --count;
        }
        std::copy_n(value.data(), count, destination.data());
        destination[count] = '\0';
    }

    lv_subject_t subject_{};
    std::array<char, Capacity> buffer_{};
    std::array<char, Capacity> previous_{};
};

#endif

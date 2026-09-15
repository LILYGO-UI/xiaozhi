#include "app.hpp"

#include <cm0/status_bar.h>
#include <cm0/typography.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kPartialRows = 80;
constexpr int kHomeIndicatorClearance = 41;
constexpr std::uint32_t kColorPageBackground = 0xf2f2f7;
constexpr std::uint32_t kColorContainerBackground = 0xffffff;
constexpr std::uint32_t kColorContainerTitle = 0x1a1a1a;
constexpr const char *kReply = "It is a good day to go out. Dress for the current weather.";

std::vector<lv_color32_t> framebuffer;
int display_width = 0;

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * width;
        const auto destination_offset = static_cast<std::size_t>(y) * display_width + area->x1;
        assert(destination_offset + width <= framebuffer.size());
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

void write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    assert(file);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    assert(file.good());
}

void settle(lv_obj_t *parent)
{
    lv_tick_inc(160);
    lv_timer_handler();
    lv_obj_update_layout(parent);
}

lv_obj_t *find(lv_obj_t *parent, const char *name)
{
    auto *object = lv_obj_find_by_name(parent, name);
    if (!object) std::cerr << "Missing UI object: " << name << '\n';
    assert(object);
    return object;
}

std::string text(lv_obj_t *label)
{
    return lv_label_get_text(label);
}

lv_area_t bounds(lv_obj_t *object)
{
    lv_area_t result;
    lv_obj_get_coords(object, &result);
    return result;
}

bool contains(const lv_area_t &outer, const lv_area_t &inner)
{
    return inner.x1 >= outer.x1 && inner.y1 >= outer.y1 && inner.x2 <= outer.x2 && inner.y2 <= outer.y2;
}

void require_contained(lv_obj_t *object, const lv_area_t &available)
{
    const auto area = bounds(object);
    if (!contains(available, area)) {
        const char *name = lv_obj_get_name(object);
        std::cerr << "Clipped " << (name ? name : "unnamed object") << ": "
                  << area.x1 << ',' << area.y1 << '-' << area.x2 << ',' << area.y2
                  << " outside " << available.x1 << ',' << available.y1 << '-'
                  << available.x2 << ',' << available.y2 << '\n';
        std::abort();
    }
}

void require_separate(lv_obj_t *first, lv_obj_t *second)
{
    const auto a = bounds(first);
    const auto b = bounds(second);
    if (!(a.x2 < b.x1 || b.x2 < a.x1 || a.y2 < b.y1 || b.y2 < a.y1)) {
        std::cerr << "Overlapping objects: " << lv_obj_get_name(first) << " and "
                  << lv_obj_get_name(second) << '\n';
        std::abort();
    }
}

void require_label_fits(lv_obj_t *label)
{
    assert(lv_obj_check_type(label, &lv_label_class));
    const auto *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    assert(font);
    lv_point_t text_size;
    lv_text_get_size(&text_size, lv_label_get_text(label), font,
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     lv_obj_get_content_width(label), LV_TEXT_FLAG_NONE);
    if (text_size.y > lv_obj_get_content_height(label)) {
        std::cerr << "Label text is clipped: " << lv_label_get_text(label) << " (needs "
                  << text_size.y << ", has " << lv_obj_get_content_height(label) << ")\n";
        std::abort();
    }
}

void require_button(lv_obj_t *button, const lv_area_t &available)
{
    require_contained(button, available);
    assert(lv_obj_get_width(button) >= 44 && lv_obj_get_height(button) >= 44);
    const auto button_area = bounds(button);
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(button); ++index) {
        auto *child = lv_obj_get_child(button, index);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN) || !lv_obj_check_type(child, &lv_label_class)) continue;
        require_contained(child, button_area);
        require_label_fits(child);
    }
}

// Match AppKit's real parent, including theme padding and the system layers.
// The old test passed the screen itself and missed the 96 px status bar inset.
lv_obj_t *create_appkit_parent(bool embedded)
{
    auto *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(kColorPageBackground), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(screen, 36, 0);
    lv_obj_set_style_clip_corner(screen, true, 0);

    auto *status_bar = cm0_status_bar_create(lv_layer_top());
    assert(status_bar);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    auto *content = lv_obj_create(screen);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_align(content, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_top(content, CM0_STATUS_BAR_HEIGHT, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_bg_color(content, lv_color_hex(kColorPageBackground), 0);
    lv_obj_set_style_bg_grad_dir(content, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    auto *home = cm0_home_indicator_create(lv_layer_top());
    assert(home);
    lv_obj_set_size(home, LV_PCT(100), 38);
    lv_obj_align(home, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_obj_remove_flag(home, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    if (!embedded) return content;

    auto *panel = lv_obj_create(content);
    lv_obj_remove_style_all(panel);
    lv_obj_set_name_static(panel, "embedded_app_parent");
    lv_obj_set_size(panel, 320, 480);
    lv_obj_center(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

struct Page {
    lv_obj_t *parent;
    bool embedded;
    lv_obj_t *surface;
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *status;
    lv_obj_t *subtitle;
    lv_obj_t *ptt;
    lv_obj_t *continuous;
    lv_obj_t *interrupt;
    lv_obj_t *history;
    lv_obj_t *overlay;
    lv_obj_t *back;
    lv_obj_t *chat;
    lv_obj_t *new_chat;
    lv_obj_t *activation;
    lv_obj_t *activation_code;

    explicit Page(lv_obj_t *root, bool is_embedded)
        : parent(root), embedded(is_embedded), surface(find(root, "app_surface")),
          header(find(root, "assistant_header")), title(find(root, "app_title")),
          status(find(root, "assistant_status")), subtitle(find(root, "assistant_subtitle")),
          ptt(find(root, "assistant_ptt")), continuous(find(root, "assistant_continuous")),
          interrupt(find(root, "assistant_interrupt")), history(find(root, "assistant_history")),
          overlay(find(root, "assistant_history_overlay")), back(find(root, "assistant_history_back")),
          chat(find(root, "assistant_chat")), new_chat(find(root, "assistant_new_chat")),
          activation(find(root, "assistant_activation_panel")), activation_code(find(root, "assistant_activation_code"))
    {
        assert(parent != lv_screen_active());
        assert(lv_obj_get_parent(surface) == parent);
    }

    lv_area_t available() const
    {
        lv_area_t area;
        lv_obj_get_content_coords(parent, &area);
        if (!embedded)
            area.y2 = std::min(area.y2, lv_display_get_vertical_resolution(nullptr) - kHomeIndicatorClearance - 1);
        return area;
    }

    void validate_main() const
    {
        const auto area = available();
        require_contained(header, area);
        require_contained(title, bounds(header));
        require_contained(status, bounds(header));
        require_label_fits(title);
        require_label_fits(status);
        require_separate(title, status);
        require_button(ptt, area);
        require_button(history, area);
        require_contained(continuous, area);
        require_separate(ptt, continuous);
        require_separate(ptt, history);
        require_separate(history, continuous);
        if (!lv_obj_has_flag(interrupt, LV_OBJ_FLAG_HIDDEN)) {
            require_button(interrupt, area);
            require_separate(ptt, interrupt);
            require_separate(continuous, interrupt);
            require_separate(history, interrupt);
        }
        if (!lv_obj_has_flag(activation, LV_OBJ_FLAG_HIDDEN)) {
            auto *subtitle_viewport = find(parent, "assistant_subtitle_scroll");
            auto *retry = find(parent, "assistant_retry");
            lv_area_t viewport_area;
            lv_obj_get_content_coords(subtitle_viewport, &viewport_area);
            require_contained(subtitle_viewport, area);
            assert(lv_obj_is_visible(retry));
            require_button(retry, area);
            require_contained(retry, viewport_area);
            require_contained(activation, area);
            require_contained(activation, viewport_area);
            require_contained(activation_code, bounds(activation));
            require_label_fits(activation_code);
            require_separate(activation, ptt);
            require_separate(activation, header);
        } else {
            // Long subtitles may scroll, but their viewport must remain clear
            // of the primary controls, and the label must hold every line.
            auto *subtitle_viewport = lv_obj_get_parent(subtitle);
            require_contained(subtitle_viewport, area);
            require_label_fits(subtitle);
            require_separate(subtitle_viewport, ptt);
            require_separate(subtitle_viewport, header);
        }
        assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
        assert(lv_obj_get_style_bg_opa(surface, LV_PART_MAIN) == LV_OPA_COVER);
        assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
        assert(lv_color_eq(lv_obj_get_style_bg_color(header, LV_PART_MAIN), lv_color_hex(kColorContainerBackground)));
        assert(lv_color_eq(lv_obj_get_style_text_color(title, LV_PART_MAIN), lv_color_hex(kColorContainerTitle)));
    }

    void validate_history() const
    {
        assert(!lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN));
        require_button(back, available());
        require_button(new_chat, available());
        require_contained(chat, available());
        require_separate(back, new_chat);
        require_separate(back, chat);
        require_separate(new_chat, chat);
        assert(lv_obj_has_flag(chat, LV_OBJ_FLAG_SCROLLABLE));
    }

    void click(lv_obj_t *object) const
    {
        lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr);
        settle(parent);
    }

    void set_continuous(bool enabled) const
    {
        lv_obj_set_state(continuous, LV_STATE_CHECKED, enabled);
        lv_obj_send_event(continuous, LV_EVENT_VALUE_CHANGED, nullptr);
        settle(parent);
        assert(lv_obj_has_state(continuous, LV_STATE_CHECKED) == enabled);
    }

    void talk() const
    {
        lv_obj_send_event(ptt, LV_EVENT_PRESSED, nullptr);
        settle(parent);
        assert(text(status) == "Listening");
        assert(text(subtitle).find("Release") != std::string::npos);
        validate_main();
        lv_obj_send_event(ptt, LV_EVENT_RELEASED, nullptr);
        settle(parent);
        assert(text(status) == "Ready to talk");
        assert(text(subtitle) == kReply);
    }

    void resize(lv_display_t *display, int width, int height) const
    {
        if (embedded) {
            lv_obj_set_size(parent, width, height);
            lv_obj_center(parent);
        } else {
            display_width = width;
            lv_display_set_resolution(display, width, height);
        }
        settle(lv_screen_active());
        assert(find(parent, "app_surface") == surface);
        assert(find(parent, "assistant_chat") == chat);
        assert(find(parent, "assistant_subtitle") == subtitle);
    }
};

void exercise_interactions(const Page &page, lv_display_t *display, int width, int height)
{
    page.talk();
    page.validate_main();
    page.set_continuous(true);
    assert(text(page.status) == "Listening");
    assert(text(page.subtitle).find("pause") != std::string::npos);
    page.validate_main();

    auto *group = lv_group_create();
    lv_group_add_obj(group, page.ptt);
    lv_group_focus_obj(page.ptt);
    const auto listening_subtitle = text(page.subtitle);
    page.resize(display, height, width);
    page.validate_main();
    assert(text(page.status) == "Listening");
    assert(text(page.subtitle) == listening_subtitle);
    assert(lv_obj_has_state(page.continuous, LV_STATE_CHECKED));
    assert(lv_group_get_focused(group) == page.ptt);
    page.resize(display, width, height);
    page.click(page.interrupt);
    assert(!lv_obj_has_state(page.continuous, LV_STATE_CHECKED));
    assert(text(page.status) == "Ready to talk");
    page.set_continuous(true);
    page.set_continuous(false);
    assert(text(page.status) == "Ready to talk");
    lv_group_delete(group);

    // Enough actual UI turns to overflow even the tall portrait history.
    for (int turn = 0; turn < 12; ++turn) page.talk();
    page.click(page.history);
    page.validate_history();
    assert(lv_obj_find_by_name(page.chat, "assistant_user_bubble"));
    assert(lv_obj_find_by_name(page.chat, "assistant_reply_bubble"));
    const auto child_count = lv_obj_get_child_count(page.chat);
    assert(lv_obj_get_scroll_bottom(page.chat) + lv_obj_get_scroll_y(page.chat) > 64);
    lv_obj_scroll_to_y(page.chat, 64, LV_ANIM_OFF);
    settle(page.parent);
    const auto scroll_y = lv_obj_get_scroll_y(page.chat);
    assert(scroll_y == 64);
    page.resize(display, height, width);
    page.validate_history();
    assert(lv_obj_get_child_count(page.chat) == child_count);
    assert(lv_obj_get_scroll_y(page.chat) == scroll_y);
    assert(text(page.subtitle) == kReply);
    page.resize(display, width, height);
    assert(lv_obj_get_scroll_y(page.chat) == scroll_y);
    page.click(page.back);
    assert(lv_obj_has_flag(page.overlay, LV_OBJ_FLAG_HIDDEN));
    page.click(page.history);
    assert(lv_obj_get_scroll_y(page.chat) == scroll_y);
    page.click(page.new_chat);
    assert(!lv_obj_find_by_name(page.chat, "assistant_user_bubble"));
    assert(!lv_obj_find_by_name(page.chat, "assistant_reply_bubble"));
    assert(text(page.subtitle).find("Hi, I'm Xiaozhi.") != std::string::npos);
    if (!lv_obj_has_flag(page.overlay, LV_OBJ_FLAG_HIDDEN)) page.click(page.back);
    page.validate_main();
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc >= 4 && argc <= 6);
    const int width = std::atoi(argv[1]);
    const int height = std::atoi(argv[2]);
    const std::string mode = argc >= 5 ? argv[4] : "default";
    const bool embedded = argc == 6 && std::string(argv[5]) == "embedded";
    assert(mode == "default" || mode == "binding" || mode == "history");
    assert(width >= 320 && height >= 240);

    display_width = width;
    framebuffer.resize(static_cast<std::size_t>(width) * height);
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(std::max(width, height)) * kPartialRows);
    lv_init();
    auto *display = lv_display_create(width, height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);
    cm0_app_context_t context{};
    context.root = create_appkit_parent(embedded);
    const auto *descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    settle(context.root);
    const Page page(context.root, embedded);
    assert(lv_obj_has_flag(page.overlay, LV_OBJ_FLAG_HIDDEN));

    if (mode == "binding") {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (text(page.status) != "Awaiting activation") {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            settle(context.root);
        }
        assert(!lv_obj_has_flag(page.activation, LV_OBJ_FLAG_HIDDEN));
        assert(text(page.activation_code) == "123456");
        page.validate_main();
        const int parent_width = embedded ? 320 : width;
        const int parent_height = embedded ? 480 : height;
        page.resize(display, parent_height, parent_width);
        page.validate_main();
        assert(text(page.activation_code) == "123456");
        page.resize(display, parent_width, parent_height);
    } else {
        assert(text(page.status) == "Ready to talk");
        assert(!text(page.subtitle).empty());
        page.validate_main();
        if (mode == "history") {
            page.talk();
            page.talk();
            page.click(page.history);
            page.validate_history();
        }
    }

    lv_tick_inc(20);
    lv_refr_now(display);
    const auto first_pixel = framebuffer.front();
    assert(std::any_of(framebuffer.begin() + 1, framebuffer.end(), [first_pixel](const auto &pixel) {
        return pixel.red != first_pixel.red || pixel.green != first_pixel.green || pixel.blue != first_pixel.blue;
    }));
    write_ppm(argv[3], width, height);

    if (mode == "default") exercise_interactions(page, display, embedded ? 320 : width, embedded ? 480 : height);
    descriptor->close();
    descriptor->close();
    assert(!lv_obj_find_by_name(context.root, "app_surface"));
    settle(context.root);
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}

#ifndef LILYGO_UI_TEMPLATE_COMPONENTS_HPP
#define LILYGO_UI_TEMPLATE_COMPONENTS_HPP

#include <lvgl.h>

#include <cstdint>

namespace components {

inline constexpr std::uint32_t color_page_background      = 0xf2f2f7;
inline constexpr std::uint32_t color_container_background = 0xffffff;
inline constexpr std::uint32_t color_container_title      = 0x1a1a1a;
inline constexpr std::uint32_t color_text                 = 0x000000;
inline constexpr std::uint32_t color_border               = 0xd8dde3;
inline constexpr std::uint32_t color_action               = 0x20262d;
inline constexpr std::uint32_t color_muted                = 0x667085;
inline constexpr std::uint32_t color_accent               = 0x16875f;
inline constexpr std::uint32_t color_accent_soft          = 0xe7f5ef;
inline constexpr std::uint32_t color_warning_soft         = 0xfff3c4;
inline constexpr std::uint32_t color_warning_text         = 0x6f5100;
inline constexpr std::uint32_t color_danger               = 0xbe394b;
inline constexpr std::uint32_t color_robot_background     = 0x111518;
inline constexpr std::uint32_t color_robot_accent         = 0x62d5a2;
// Warm face illustration: opaque halo, skin, cheeks, and facial features.
inline constexpr std::uint32_t color_face_halo            = 0xfff7e9;
inline constexpr std::uint32_t color_face_skin            = 0xffd796;
inline constexpr std::uint32_t color_face_cheek           = 0xf3aa81;
inline constexpr std::uint32_t color_face_ink             = 0x593d2c;
inline constexpr std::uint32_t color_status_wait          = 0xae741b;

lv_obj_t *create_page(lv_obj_t *parent);
lv_obj_t *create_header(lv_obj_t *parent, const char *title);
lv_obj_t *create_card(lv_obj_t *parent);
lv_obj_t *create_button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, void *user_data);
lv_obj_t *create_icon_button(lv_obj_t *parent, const char *icon, const char *accessible_name, lv_event_cb_t callback,
                             void *user_data);

}  // namespace components

#endif

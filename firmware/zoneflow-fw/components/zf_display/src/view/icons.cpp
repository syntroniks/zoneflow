// SPDX-License-Identifier: Apache-2.0
#include "zf_view/icons.hpp"

#include <cmath>
#include <cstdint>

namespace zf::view {
namespace {

using ui::Icon;

// Draws in icon units (0..24) onto the layer, scaled to the object size.
class Pen {
public:
    Pen(lv_layer_t *layer, const lv_area_t &area, lv_color_t color)
        : layer_(layer), x0_(area.x1), y0_(area.y1), size_(lv_area_get_width(&area)), color_(color)
    {
    }

    void line(float x1, float y1, float x2, float y2, float width = 2.4f) const
    {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = color_;
        d.width = stroke(width);
        d.round_start = 1;
        d.round_end = 1;
        d.p1.x = px(x1);
        d.p1.y = py(y1);
        d.p2.x = px(x2);
        d.p2.y = py(y2);
        lv_draw_line(layer_, &d);
    }

    // Angles in degrees, 0 = 3 o'clock, clockwise.
    void arc(float cx, float cy, float radius, int start, int end, float width = 2.4f) const
    {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.color = color_;
        d.width = stroke(width);
        d.center.x = px(cx);
        d.center.y = py(cy);
        // lv_draw_arc takes the outer radius; icons give the centre line.
        d.radius = static_cast<uint16_t>(len(radius) + d.width / 2);
        d.start_angle = start;
        d.end_angle = end;
        d.rounded = 1;
        lv_draw_arc(layer_, &d);
    }

    void ring(float cx, float cy, float radius, float width = 2.4f) const { arc(cx, cy, radius, 0, 360, width); }

    void fill(float x, float y, float w, float h, float radius = 0.0f) const
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = color_;
        d.bg_opa = LV_OPA_COVER;
        d.radius = len(radius);
        const lv_area_t a = {px(x), py(y), px(x + w) - 1, py(y + h) - 1};
        lv_draw_rect(layer_, &d, &a);
    }

    void outline(float x, float y, float w, float h, float radius, float width = 2.0f) const
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = color_;
        d.border_opa = LV_OPA_COVER;
        d.border_width = stroke(width);
        d.radius = len(radius);
        const lv_area_t a = {px(x), py(y), px(x + w) - 1, py(y + h) - 1};
        lv_draw_rect(layer_, &d, &a);
    }

    void disc(float cx, float cy, float radius) const
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = color_;
        d.bg_opa = LV_OPA_COVER;
        d.radius = LV_RADIUS_CIRCLE;
        const int r = len(radius);
        const lv_area_t a = {px(cx) - r, py(cy) - r, px(cx) + r, py(cy) + r};
        lv_draw_rect(layer_, &d, &a);
    }

    void triangle(float x1, float y1, float x2, float y2, float x3, float y3) const
    {
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.color = color_;
        d.opa = LV_OPA_COVER;
        d.p[0].x = px(x1);
        d.p[0].y = py(y1);
        d.p[1].x = px(x2);
        d.p[1].y = py(y2);
        d.p[2].x = px(x3);
        d.p[2].y = py(y3);
        lv_draw_triangle(layer_, &d);
    }

private:
    int len(float v) const { return static_cast<int>(std::lround(v * static_cast<float>(size_) / 24.0f)); }
    int32_t px(float v) const { return x0_ + len(v); }
    int32_t py(float v) const { return y0_ + len(v); }
    int32_t stroke(float v) const
    {
        const int w = len(v);
        return w < 2 ? 2 : w;
    }

    lv_layer_t *layer_;
    int32_t x0_;
    int32_t y0_;
    int32_t size_;
    lv_color_t color_;
};

void wifi(const Pen &p)
{
    p.disc(12, 19.5f, 1.8f);
    p.arc(12, 20, 6, 228, 312);
    p.arc(12, 20, 11, 228, 312);
    p.arc(12, 20, 16, 228, 312);
}

void draw(const Pen &p, Icon icon)
{
    switch (icon) {
    case Icon::Drop:
        p.disc(12, 15, 6.5f);
        p.triangle(12, 2, 6.2f, 12.5f, 17.8f, 12.5f);
        break;
    case Icon::Clock:
        p.ring(12, 12, 9);
        p.line(12, 12, 12, 7);
        p.line(12, 12, 16, 14);
        break;
    case Icon::Calendar:
        p.outline(3, 5, 18, 17, 3);
        p.fill(3, 5, 18, 5, 2);
        p.line(8, 2.5f, 8, 6);
        p.line(16, 2.5f, 16, 6);
        p.fill(7, 13, 3, 3, 0.5f);
        p.fill(14, 13, 3, 3, 0.5f);
        break;
    case Icon::Rain:
        p.disc(8, 10, 4.5f);
        p.disc(14, 8, 5.5f);
        p.disc(18, 11, 3.5f);
        p.fill(7, 10, 12, 4.5f, 2);
        p.line(8, 18, 7, 21.5f, 2.0f);
        p.line(12.5f, 18, 11.5f, 21.5f, 2.0f);
        p.line(17, 18, 16, 21.5f, 2.0f);
        break;
    case Icon::Wifi:
        wifi(p);
        break;
    case Icon::WifiOff:
        wifi(p);
        p.line(4, 4, 20, 21, 2.8f);
        break;
    case Icon::Fault:
        p.line(12, 3.5f, 21.5f, 20.5f);
        p.line(21.5f, 20.5f, 2.5f, 20.5f);
        p.line(2.5f, 20.5f, 12, 3.5f);
        p.line(12, 9.5f, 12, 14.5f);
        p.disc(12, 17.6f, 1.3f);
        break;
    case Icon::Sprinkler:
        p.fill(10, 14, 4, 8, 1);
        p.fill(6.5f, 11, 11, 3.5f, 1.5f);
        p.line(12, 8, 12, 3, 2.0f);
        p.line(8, 8, 4.5f, 4, 2.0f);
        p.line(16, 8, 19.5f, 4, 2.0f);
        break;
    case Icon::Gauge:
        p.arc(12, 16, 9, 180, 360);
        p.line(12, 16, 16, 10);
        p.disc(12, 16, 2);
        break;
    case Icon::Sliders:
        p.line(3.5f, 6, 20.5f, 6, 2.0f);
        p.line(3.5f, 12, 20.5f, 12, 2.0f);
        p.line(3.5f, 18, 20.5f, 18, 2.0f);
        p.disc(8, 6, 3);
        p.disc(16, 12, 3);
        p.disc(10, 18, 3);
        break;
    case Icon::Play:
        p.triangle(7, 4, 7, 20, 20, 12);
        break;
    case Icon::Stop:
        p.fill(5, 5, 14, 14, 2);
        break;
    case Icon::Pause:
        p.fill(6, 4.5f, 4.5f, 15, 1);
        p.fill(13.5f, 4.5f, 4.5f, 15, 1);
        break;
    case Icon::Skip:
        p.triangle(4, 4.5f, 4, 19.5f, 15, 12);
        p.fill(16.5f, 4.5f, 3.5f, 15, 1);
        break;
    case Icon::Menu:
        p.line(4.5f, 6, 19.5f, 6, 2.6f);
        p.line(4.5f, 12, 19.5f, 12, 2.6f);
        p.line(4.5f, 18, 19.5f, 18, 2.6f);
        break;
    case Icon::Check:
        p.line(4.5f, 12.5f, 10, 18, 3.0f);
        p.line(10, 18, 19.5f, 6.5f, 3.0f);
        break;
    case Icon::Lock:
        p.fill(5, 11, 14, 10.5f, 2);
        p.arc(12, 11, 4.5f, 180, 360);
        break;
    case Icon::Sun:
        p.disc(12, 12, 4);
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.7853982f;
            p.line(12 + 7.2f * std::cos(a), 12 + 7.2f * std::sin(a), 12 + 10 * std::cos(a), 12 + 10 * std::sin(a),
                   2.0f);
        }
        break;
    case Icon::Info:
        p.ring(12, 12, 9);
        p.line(12, 11, 12, 16.5f);
        p.disc(12, 7.5f, 1.3f);
        break;
    case Icon::Bolt:
        p.triangle(14, 2, 5, 13.5f, 12.5f, 13.5f);
        p.triangle(10, 22, 19, 10.5f, 11.5f, 10.5f);
        break;
    case Icon::Flow:
        p.arc(7.5f, 9, 3.5f, 180, 360, 2.2f);
        p.arc(14.5f, 9, 3.5f, 0, 180, 2.2f);
        p.arc(7.5f, 16.5f, 3.5f, 180, 360, 2.2f);
        p.arc(14.5f, 16.5f, 3.5f, 0, 180, 2.2f);
        break;
    case Icon::ChevronRight:
        p.line(9, 5.5f, 15.5f, 12, 2.6f);
        p.line(15.5f, 12, 9, 18.5f, 2.6f);
        break;
    case Icon::ChevronLeft:
        p.line(15, 5.5f, 8.5f, 12, 2.6f);
        p.line(8.5f, 12, 15, 18.5f, 2.6f);
        break;
    case Icon::Knob:
        p.ring(12, 13, 6);
        p.line(12, 13, 12, 9.5f);
        p.arc(12, 13, 10.5f, 215, 325, 2.0f);
        p.line(20.6f, 7, 20.9f, 3, 2.0f);
        p.line(20.6f, 7, 16.8f, 6.2f, 2.0f);
        break;
    case Icon::Trash:
        p.line(4, 6.5f, 20, 6.5f);
        p.line(9.5f, 3.5f, 14.5f, 3.5f);
        p.line(6.5f, 7, 7.8f, 20.5f, 2.2f);
        p.line(7.8f, 20.5f, 16.2f, 20.5f, 2.2f);
        p.line(16.2f, 20.5f, 17.5f, 7, 2.2f);
        p.line(10.3f, 10.5f, 10.5f, 16.5f, 2.0f);
        p.line(13.7f, 10.5f, 13.5f, 16.5f, 2.0f);
        break;
    case Icon::Plus:
        p.line(12, 4.5f, 12, 19.5f, 2.8f);
        p.line(4.5f, 12, 19.5f, 12, 2.8f);
        break;
    case Icon::None:
    case Icon::Count:
        break;
    }
}

void onDraw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    const auto icon = static_cast<Icon>(reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(obj)));
    if (icon == Icon::None) {
        return;
    }
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const Pen pen(lv_event_get_layer(e), area, lv_obj_get_style_text_color(obj, LV_PART_MAIN));
    draw(pen, icon);
}

} // namespace

lv_obj_t *iconCreate(lv_obj_t *parent, int size)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_hidden(obj, true);
    lv_obj_set_user_data(obj, nullptr);
    lv_obj_add_event_cb(obj, onDraw, LV_EVENT_DRAW_MAIN, nullptr);
    return obj;
}

void iconSet(lv_obj_t *obj, ui::Icon icon, lv_color_t color)
{
    const auto current = static_cast<Icon>(reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(obj)));
    const bool hidden = lv_obj_is_hidden(obj);
    if (icon == Icon::None) {
        if (!hidden) {
            lv_obj_set_hidden(obj, true);
        }
        lv_obj_set_user_data(obj, nullptr);
        return;
    }
    const bool sameColor = lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), color);
    if (current == icon && sameColor && !hidden) {
        return;
    }
    lv_obj_set_user_data(obj, reinterpret_cast<void *>(static_cast<std::uintptr_t>(icon)));
    if (!sameColor) {
        lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    }
    if (hidden) {
        lv_obj_set_hidden(obj, false);
    }
    lv_obj_invalidate(obj);
}

} // namespace zf::view

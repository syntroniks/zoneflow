// SPDX-License-Identifier: Apache-2.0
#include "sim.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// LVGL's bundled lodepng (LV_USE_LODEPNG). Its header also declares a C++
// interface that does not compile inside LVGL's extern "C" wrapper, so the
// one C function used here is declared directly. The result is allocated
// with lv_malloc. (lodepng_encode24_file is no use: in LVGL it writes
// through lv_fs drive letters.)
extern "C" unsigned lodepng_encode24(unsigned char **out, size_t *outsize, const unsigned char *image, unsigned w,
                                     unsigned h);

namespace zf::sim {

uint32_t Screen::tickMs = 0;

Screen::Screen()
{
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return Screen::tickMs; });
    display_ = lv_display_create(kWidth, kHeight);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_user_data(display_, this);
    lv_display_set_flush_cb(display_, &Screen::flush);
    lv_display_set_buffers(display_, draw_.data(), nullptr, static_cast<uint32_t>(draw_.size() * sizeof(uint16_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    view_.create(display_);
}

void Screen::flush(lv_display_t *display, const lv_area_t *area, uint8_t *px)
{
    auto *self = static_cast<Screen *>(lv_display_get_user_data(display));
    const int w = lv_area_get_width(area);
    const auto *src = reinterpret_cast<const uint16_t *>(px);
    for (int y = area->y1; y <= area->y2; ++y) {
        std::memcpy(&self->frame_[static_cast<std::size_t>(y * kWidth + area->x1)], src,
                    static_cast<std::size_t>(w) * sizeof(uint16_t));
        src += w;
    }
    lv_display_flush_ready(display);
}

void Screen::render(const ui::Frame &frame)
{
    view_.render(frame);
    lv_refr_now(display_);
}

bool Screen::savePng(const std::string &path, int scale, int backlightPercent) const
{
    scale = scale < 1 ? 1 : scale;
    const unsigned w = static_cast<unsigned>(kWidth * scale);
    const unsigned h = static_cast<unsigned>(kHeight * scale);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
    // A dimmed backlight still reads well on the panel; a linear scale of
    // the pixel values would look far darker than that. This is an
    // illustration, not a measurement.
    const float level = backlightPercent >= 100 ? 1.0f : std::pow(static_cast<float>(backlightPercent) / 100.0f, 0.45f);
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            const uint16_t p = frame_[(y / static_cast<unsigned>(scale)) * kWidth + x / static_cast<unsigned>(scale)];
            // RGB565 to 8 bits per channel, replicating the top bits.
            const unsigned r5 = (p >> 11) & 0x1F;
            const unsigned g6 = (p >> 5) & 0x3F;
            const unsigned b5 = p & 0x1F;
            unsigned char *out = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
            out[0] = static_cast<unsigned char>(static_cast<float>((r5 << 3) | (r5 >> 2)) * level);
            out[1] = static_cast<unsigned char>(static_cast<float>((g6 << 2) | (g6 >> 4)) * level);
            out[2] = static_cast<unsigned char>(static_cast<float>((b5 << 3) | (b5 >> 2)) * level);
        }
    }
    unsigned char *png = nullptr;
    size_t size = 0;
    if (lodepng_encode24(&png, &size, rgb.data(), w, h) != 0 || png == nullptr) {
        return false;
    }
    bool ok = false;
    if (std::FILE *f = std::fopen(path.c_str(), "wb")) {
        ok = std::fwrite(png, 1, size, f) == size;
        ok = std::fclose(f) == 0 && ok;
    }
    lv_free(png);
    return ok;
}

World::World(bool wizardDone, bool clockValid, bool wifi, bool revA)
{
    if (revA) {
        h.useRevABoard();
    }
    h.clock.utc = test::utcOf(2026, 11, 10, 13, 32); // 14:32 CET
    h.system.graphics = std::to_string(LVGL_VERSION_MAJOR) + "." + std::to_string(LVGL_VERSION_MINOR) + "." +
                        std::to_string(LVGL_VERSION_PATCH);
    if (wifi) {
        h.net.connect();
    }
    h.diag.learned = {255, 262, 0, 248, 240, 0, 0, 0};
    h.start(test::demoConfig(), wizardDone, clockValid);
}

} // namespace zf::sim

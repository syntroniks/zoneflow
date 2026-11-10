// SPDX-License-Identifier: Apache-2.0
#include "zf_view/theme.hpp"

namespace zf::view {

// Contrast ratios (WCAG 2 relative-luminance formula, computed from the sRGB
// values below; they do not predict how a particular panel renders them):
//   on bg:        text 17.7, muted 8.9, running 8.8, warn 11.2, fault 6.9, ok 10.5
//   on surfaceHi: text 12.9          on surface: muted 7.9
//   onFocus on a tone fill equals that tone on bg (onFocus is the bg colour).
// The fault red on the white focus fill would be only 2.6, so a focused row
// draws all its text in onFocus and loses the colour, never the word.
const Palette &palette()
{
    static const Palette p = {
        lv_color_hex(0x0A0E13), // bg
        lv_color_hex(0x161C24), // surface
        lv_color_hex(0x232C37), // surfaceHi
        lv_color_hex(0x3A4654), // line
        lv_color_hex(0xF2F5F8), // text
        lv_color_hex(0xA7B1BD), // muted
        lv_color_hex(0xF2F5F8), // focus
        lv_color_hex(0x0A0E13), // onFocus
        lv_color_hex(0x3DB8FF), // running
        lv_color_hex(0xFFB829), // warn
        lv_color_hex(0xFF6A5E), // fault
        lv_color_hex(0x5BD68A), // ok
    };
    return p;
}

lv_color_t toneColor(ui::Tone tone)
{
    const Palette &p = palette();
    switch (tone) {
    case ui::Tone::Normal:
        return p.text;
    case ui::Tone::Muted:
        return p.muted;
    case ui::Tone::Running:
        return p.running;
    case ui::Tone::Warn:
        return p.warn;
    case ui::Tone::Fault:
        return p.fault;
    case ui::Tone::Ok:
        return p.ok;
    }
    return p.text;
}

} // namespace zf::view

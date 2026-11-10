// SPDX-License-Identifier: Apache-2.0
#include "zf_view/view.hpp"

#include <algorithm>
#include <cstring>

#include "zf_view/icons.hpp"
#include "zf_view/theme.hpp"

namespace zf::view {
namespace {

using ui::Icon;
using ui::Tone;

constexpr int kContentH = kScreenH - kBarH; // 210
constexpr int kRowH = 42;
constexpr int kButtonH = 32;
constexpr int kButtonY = 174;
constexpr int kMargin = 8;

// ---- small helpers: every setter skips work when nothing changes --------------------

lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    return o;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, color, LV_PART_MAIN);
    lv_label_set_text(l, "");
    return l;
}

// A single-line label that ends in dots when the text does not fit.
lv_obj_t *lineLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, int x, int y, int w)
{
    lv_obj_t *l = label(parent, font, color);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, w, lv_font_get_line_height(font));
    return l;
}

void setText(lv_obj_t *l, const std::string &text)
{
    if (std::strcmp(lv_label_get_text(l), text.c_str()) != 0) {
        lv_label_set_text(l, text.c_str());
    }
}

void show(lv_obj_t *o, bool visible)
{
    if (lv_obj_is_hidden(o) == visible) {
        lv_obj_set_hidden(o, !visible);
    }
}

void setTextColor(lv_obj_t *o, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_text_color(o, c, LV_PART_MAIN);
    }
}

void setFont(lv_obj_t *o, const lv_font_t *font)
{
    if (lv_obj_get_style_text_font(o, LV_PART_MAIN) != font) {
        lv_obj_set_style_text_font(o, font, LV_PART_MAIN);
    }
}

void setFill(lv_obj_t *o, lv_color_t c, bool filled = true)
{
    const lv_opa_t opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != opa) {
        lv_obj_set_style_bg_opa(o, opa, LV_PART_MAIN);
    }
    if (filled && !lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_bg_color(o, c, LV_PART_MAIN);
    }
}

void setBorder(lv_obj_t *o, lv_color_t c, int width)
{
    if (lv_obj_get_style_border_width(o, LV_PART_MAIN) != width) {
        lv_obj_set_style_border_width(o, width, LV_PART_MAIN);
        lv_obj_set_style_border_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    }
    if (width > 0 && !lv_color_eq(lv_obj_get_style_border_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_border_color(o, c, LV_PART_MAIN);
    }
}

void setPos(lv_obj_t *o, int x, int y)
{
    if (lv_obj_get_style_x(o, LV_PART_MAIN) != x || lv_obj_get_style_y(o, LV_PART_MAIN) != y) {
        lv_obj_set_pos(o, x, y);
    }
}

void setWidth(lv_obj_t *o, int w)
{
    if (lv_obj_get_style_width(o, LV_PART_MAIN) != w) {
        lv_obj_set_width(o, w);
    }
}

int textWidth(const std::string &text, const lv_font_t *font)
{
    if (text.empty()) {
        return 0;
    }
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int lineHeight(const lv_font_t *font) { return lv_font_get_line_height(font); }

} // namespace

// ---- construction ------------------------------------------------------------------------

void View::create(lv_display_t *display)
{
    const Palette &p = palette();
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_remove_style_all(screen);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, p.bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, p.text, LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, fontBody(), LV_PART_MAIN);

    createHome(screen);
    createList(screen);
    createDetail(screen);
    createPicker(screen);
    createBar(screen);
    createToast(screen);
}

void View::createBar(lv_obj_t *screen)
{
    const Palette &p = palette();
    bar_ = box(screen, 0, 0, kScreenW, kBarH);
    setFill(bar_, p.surface);
    const int bodyY = (kBarH - lineHeight(fontBody())) / 2;
    const int smallY = (kBarH - lineHeight(fontSmall())) / 2;
    barBack_ = iconCreate(bar_, 16);
    lv_obj_set_pos(barBack_, 4, (kBarH - 16) / 2);
    barTitle_ = lineLabel(bar_, fontBody(), p.text, kMargin, bodyY, 200);
    barDate_ = label(bar_, fontSmall(), p.muted);
    lv_obj_set_y(barDate_, smallY + 1);
    barStep_ = label(bar_, fontSmall(), p.muted);
    lv_obj_set_y(barStep_, smallY + 1);
    barClock_ = label(bar_, fontSmall(), p.muted);
    lv_obj_set_y(barClock_, smallY);
    barRun_ = box(bar_, 0, 4, 10, kBarH - 8);
    lv_obj_set_style_radius(barRun_, 11, LV_PART_MAIN);
    barRunIcon_ = iconCreate(barRun_, 14);
    lv_obj_set_pos(barRunIcon_, 7, (kBarH - 8 - 14) / 2);
    barRunLabel_ = label(barRun_, fontSmall(), p.onFocus);
    lv_obj_set_pos(barRunLabel_, 25, (kBarH - 8 - lineHeight(fontSmall())) / 2);
    barWifi_ = iconCreate(bar_, 20);
    barRain_ = iconCreate(bar_, 20);
    barFault_ = iconCreate(bar_, 20);
    for (lv_obj_t *icon : {barWifi_, barRain_, barFault_}) {
        lv_obj_set_y(icon, (kBarH - 20) / 2);
    }
}

void View::createButtons(lv_obj_t *parent, ButtonBar &bar)
{
    const Palette &p = palette();
    for (int i = 0; i < kMaxButtons; ++i) {
        bar.box[i] = box(parent, 0, kButtonY, 10, kButtonH);
        lv_obj_set_style_radius(bar.box[i], 8, LV_PART_MAIN);
        bar.icon[i] = iconCreate(bar.box[i], 16);
        lv_obj_set_y(bar.icon[i], (kButtonH - 16) / 2);
        bar.label[i] = label(bar.box[i], fontBody(), p.text);
        lv_obj_set_y(bar.label[i], (kButtonH - lineHeight(fontBody())) / 2);
    }
}

void View::createHome(lv_obj_t *screen)
{
    const Palette &p = palette();
    home_ = box(screen, 0, kBarH, kScreenW, kContentH);

    chip_ = box(home_, kMargin, 6, 10, 22);
    lv_obj_set_style_radius(chip_, 11, LV_PART_MAIN);
    chipIcon_ = iconCreate(chip_, 14);
    lv_obj_set_pos(chipIcon_, 8, 4);
    chipLabel_ = label(chip_, fontSmall(), p.onFocus);
    lv_obj_set_y(chipLabel_, (22 - lineHeight(fontSmall())) / 2);
    chipNote_ = lineLabel(home_, fontSmall(), p.muted, 0, 6 + (22 - lineHeight(fontSmall())) / 2, 100);

    badge_ = box(home_, kMargin, 33, 30, 30);
    lv_obj_set_style_radius(badge_, 8, LV_PART_MAIN);
    setFill(badge_, p.running);
    badgeLabel_ = label(badge_, fontBody(), p.onFocus);
    lv_obj_set_width(badgeLabel_, 30);
    lv_obj_set_style_text_align(badgeLabel_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_y(badgeLabel_, (30 - lineHeight(fontBody())) / 2);
    homeTitle_ = lineLabel(home_, fontTitle(), p.text, kMargin, 33, kScreenW - 2 * kMargin);
    homeTitleIcon_ = iconCreate(home_, 28);
    lv_obj_set_pos(homeTitleIcon_, kMargin, 34);

    for (lv_obj_t *&d : digits_) {
        d = label(home_, fontNumerals(), p.text);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_y(d, 65);
    }
    bigNote_ = label(home_, fontBody(), p.muted);

    progress_ = box(home_, kMargin, 121, kScreenW - 2 * kMargin, 6);
    lv_obj_set_style_radius(progress_, 3, LV_PART_MAIN);
    setFill(progress_, p.surfaceHi);
    progressFill_ = box(progress_, 0, 0, 6, 6);
    lv_obj_set_style_radius(progressFill_, 3, LV_PART_MAIN);
    setFill(progressFill_, p.running);

    const int lineY[2] = {130, 151};
    for (int i = 0; i < 2; ++i) {
        InfoLineView &v = lines_[i];
        v.box = box(home_, kMargin, lineY[i], kScreenW - 2 * kMargin, 21);
        lv_obj_set_style_radius(v.box, 6, LV_PART_MAIN);
        v.icon = iconCreate(v.box, 16);
        lv_obj_set_pos(v.icon, 0, 2);
        v.label = lineLabel(v.box, fontSmall(), p.text, 22, (21 - lineHeight(fontSmall())) / 2, 280);
    }
    createButtons(home_, homeButtons_);
}

void View::createList(lv_obj_t *screen)
{
    const Palette &p = palette();
    list_ = box(screen, 0, kBarH, kScreenW, kContentH);
    for (int i = 0; i < kRows; ++i) {
        RowView &r = rows_[i];
        r.box = box(list_, 4, i * kRowH + 1, 306, kRowH - 2);
        lv_obj_set_style_radius(r.box, 8, LV_PART_MAIN);
        r.icon = iconCreate(r.box, 22);
        lv_obj_set_pos(r.icon, 9, (kRowH - 2 - 22) / 2);
        r.badge = box(r.box, 7, (kRowH - 2 - 26) / 2, 26, 26);
        lv_obj_set_style_radius(r.badge, 7, LV_PART_MAIN);
        r.badgeLabel = label(r.badge, fontBody(), p.text);
        lv_obj_set_width(r.badgeLabel, 26);
        lv_obj_set_style_text_align(r.badgeLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_y(r.badgeLabel, (26 - lineHeight(fontBody())) / 2);
        r.label = lineLabel(r.box, fontBody(), p.text, 42, (kRowH - 2 - lineHeight(fontBody())) / 2, 200);
        r.value = label(r.box, fontBody(), p.muted);
        lv_obj_set_y(r.value, (kRowH - 2 - lineHeight(fontBody())) / 2);
        r.valueIcon = iconCreate(r.box, 18);
        lv_obj_set_y(r.valueIcon, (kRowH - 2 - 18) / 2);
        r.rule = box(r.box, 4, kRowH - 4, 298, 1);
        setFill(r.rule, p.line);
    }
    scrollTrack_ = box(list_, 314, 4, 3, kContentH - 8);
    lv_obj_set_style_radius(scrollTrack_, 2, LV_PART_MAIN);
    setFill(scrollTrack_, p.surfaceHi);
    scrollThumb_ = box(scrollTrack_, 0, 0, 3, 20);
    lv_obj_set_style_radius(scrollThumb_, 2, LV_PART_MAIN);
    setFill(scrollThumb_, p.muted);
}

void View::createDetail(lv_obj_t *screen)
{
    const Palette &p = palette();
    detail_ = box(screen, 0, kBarH, kScreenW, kContentH);
    detailIcon_ = iconCreate(detail_, 36);
    lv_obj_set_pos(detailIcon_, 10, 10);
    detailHeadline_ = label(detail_, fontBody(), p.text);
    lv_label_set_long_mode(detailHeadline_, LV_LABEL_LONG_MODE_WRAP);
    detailBody_ = label(detail_, fontSmall(), p.text);
    lv_label_set_long_mode(detailBody_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(detailBody_, 2, LV_PART_MAIN);

    // The QR code sits on a white card that is also its quiet zone.
    // 136 px holds up to 45 modules at 3 px each (QR version 7); the
    // provisioning payload of about 100 bytes needs version 6 (41 modules).
    qrFrame_ = box(detail_, 6, 8, 152, 152);
    lv_obj_set_style_radius(qrFrame_, 8, LV_PART_MAIN);
    setFill(qrFrame_, lv_color_white());
    qr_ = lv_qrcode_create(qrFrame_);
    lv_qrcode_set_size(qr_, 136);
    lv_qrcode_set_dark_color(qr_, lv_color_black());
    lv_qrcode_set_light_color(qr_, lv_color_white());
    lv_obj_set_pos(qr_, 8, 8);
    createButtons(detail_, detailButtons_);
}

void View::createPicker(lv_obj_t *screen)
{
    const Palette &p = palette();
    picker_ = box(screen, 0, kBarH, kScreenW, kContentH);
    setFill(picker_, p.bg);
    pickerCaption_ = label(picker_, fontBody(), p.text);
    lv_label_set_long_mode(pickerCaption_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(pickerCaption_, kScreenW - 24);
    lv_obj_set_pos(pickerCaption_, 12, 12);
    lv_obj_set_style_text_align(pickerCaption_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    pickerLeft_ = iconCreate(picker_, 20);
    pickerRight_ = iconCreate(picker_, 20);
    for (int i = 0; i < kSegments; ++i) {
        segBox_[i] = box(picker_, 0, 0, 10, 10);
        segLabel_[i] = label(segBox_[i], fontNumerals(), p.text);
        lv_obj_set_style_text_align(segLabel_[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    pickerUnit_ = label(picker_, fontTitle(), p.muted);
    pickerHint_ = label(picker_, fontSmall(), p.muted);
    lv_label_set_long_mode(pickerHint_, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(pickerHint_, kScreenW - 24);
    lv_obj_set_pos(pickerHint_, 12, 172);
    lv_obj_set_style_text_align(pickerHint_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    show(picker_, false);
}

void View::createToast(lv_obj_t *screen)
{
    toast_ = box(screen, 0, 0, kScreenW, kBarH);
    toastIcon_ = iconCreate(toast_, 18);
    lv_obj_set_pos(toastIcon_, kMargin, (kBarH - 18) / 2);
    toastLabel_ = lineLabel(toast_, fontBody(), palette().onFocus, 34, (kBarH - lineHeight(fontBody())) / 2,
                            kScreenW - 34 - kMargin);
    show(toast_, false);
}

// ---- rendering -----------------------------------------------------------------------------

void View::render(const ui::Frame &f)
{
    renderBar(f.bar);
    show(home_, f.kind == ui::FrameKind::Home && !f.pickerOpen);
    show(list_, f.kind == ui::FrameKind::List && !f.pickerOpen);
    show(detail_, f.kind == ui::FrameKind::Detail && !f.pickerOpen);
    show(picker_, f.pickerOpen);
    if (f.pickerOpen) {
        renderPicker(f.picker);
    } else if (f.kind == ui::FrameKind::Home) {
        renderHome(f.home);
    } else if (f.kind == ui::FrameKind::List) {
        renderList(f.list);
    } else {
        renderDetail(f.detail);
    }
    show(toast_, f.toastOpen);
    if (f.toastOpen) {
        renderToast(f.toast);
    }
}

void View::renderBar(const ui::StatusBarModel &b)
{
    const Palette &p = palette();
    // Right side, laid out from the edge inwards.
    int right = kScreenW - kMargin;
    auto placeIcon = [&right](lv_obj_t *icon, Icon which, lv_color_t color) {
        iconSet(icon, which, color);
        if (which != Icon::None) {
            right -= 20;
            setPos(icon, right, (kBarH - 20) / 2);
            right -= 6;
        }
    };
    placeIcon(barWifi_, b.wifi, toneColor(b.wifiTone));
    placeIcon(barRain_, b.rainHold ? Icon::Rain : Icon::None, p.warn);
    placeIcon(barFault_, b.fault ? Icon::Fault : Icon::None, p.fault);

    const bool run = !b.home && !b.run.empty();
    show(barRun_, run);
    if (run) {
        const bool running = b.runTone == Tone::Running;
        setText(barRunLabel_, b.run);
        iconSet(barRunIcon_, running ? Icon::Drop : Icon::Pause, p.onFocus);
        setFill(barRun_, toneColor(b.runTone));
        const int w = 25 + textWidth(b.run, fontSmall()) + 9;
        right -= w;
        setWidth(barRun_, w);
        setPos(barRun_, right, 4);
        right -= 8;
    }

    // The clock: large on Home, small elsewhere when there is room.
    int left = kMargin;
    show(barBack_, false);
    if (b.home) {
        setFont(barTitle_, fontBody());
        setText(barTitle_, b.clock);
        setPos(barTitle_, left, (kBarH - lineHeight(fontBody())) / 2);
        const int clockW = textWidth(b.clock, fontBody());
        setWidth(barTitle_, clockW + 2);
        setText(barDate_, b.date);
        lv_obj_set_x(barDate_, left + clockW + 10);
        show(barDate_, true);
        show(barStep_, false);
        show(barClock_, false);
        return;
    }
    show(barDate_, false);
    if (b.back) {
        iconSet(barBack_, Icon::ChevronLeft, p.muted);
        left = 24;
    } else {
        iconSet(barBack_, Icon::None, p.muted);
    }
    const int titleW = textWidth(b.title, fontBody());
    const int stepW = b.step.empty() ? 0 : textWidth(b.step, fontSmall()) + 8;
    const int clockW = textWidth(b.clock, fontSmall());
    // Show the small clock only if title, step and clock all fit.
    const bool clock = left + titleW + stepW + 12 + clockW <= right;
    show(barClock_, clock);
    if (clock) {
        setText(barClock_, b.clock);
        right -= clockW;
        lv_obj_set_x(barClock_, right);
        right -= 8;
    }
    const int maxTitle = right - left - stepW;
    setText(barTitle_, b.title);
    setPos(barTitle_, left, (kBarH - lineHeight(fontBody())) / 2);
    setWidth(barTitle_, std::max(20, std::min(titleW + 2, maxTitle)));
    show(barStep_, !b.step.empty());
    if (!b.step.empty()) {
        setText(barStep_, b.step);
        lv_obj_set_x(barStep_, left + std::min(titleW + 2, maxTitle) + 8);
    }
}

void View::renderButtons(ButtonBar &bar, const std::vector<ui::Button> &buttons, int focus)
{
    const Palette &p = palette();
    const int n = std::min<int>(static_cast<int>(buttons.size()), kMaxButtons);
    constexpr int kGap = 6;
    constexpr int kMinPad = 7;
    const int available = kScreenW - 2 * kMargin - (n > 0 ? (n - 1) * kGap : 0);
    // Every button gets the same padding around its content, and together
    // they fill the row. Icons are dropped when the labels need the room.
    bool icons = true;
    int content[kMaxButtons] = {};
    int pad = 0;
    for (int pass = 0; pass < 2; ++pass) {
        int total = 0;
        for (int i = 0; i < n; ++i) {
            const ui::Button &b = buttons[static_cast<std::size_t>(i)];
            content[i] = textWidth(b.label, fontBody()) + (icons && b.icon != Icon::None ? 22 : 0);
            total += content[i];
        }
        pad = n > 0 ? (available - total) / (2 * n) : 0;
        if (pad >= kMinPad || !icons) {
            break;
        }
        icons = false;
    }
    pad = std::max(pad, 2);
    int x = kMargin;
    for (int i = 0; i < kMaxButtons; ++i) {
        show(bar.box[i], i < n);
        if (i >= n) {
            continue;
        }
        const ui::Button &b = buttons[static_cast<std::size_t>(i)];
        const bool focused = i == focus;
        const int w = i == n - 1 ? kScreenW - kMargin - x : content[i] + 2 * pad;
        setPos(bar.box[i], x, kButtonY);
        setWidth(bar.box[i], w);
        setFill(bar.box[i], focused ? p.focus : p.surfaceHi);
        const bool icon = icons && b.icon != Icon::None;
        int cx = (w - content[i]) / 2;
        iconSet(bar.icon[i], icon ? b.icon : Icon::None, focused ? p.onFocus : toneColor(b.tone));
        if (icon) {
            lv_obj_set_x(bar.icon[i], cx);
            cx += 22;
        }
        setText(bar.label[i], b.label);
        setTextColor(bar.label[i], focused ? p.onFocus : p.text);
        lv_obj_set_x(bar.label[i], cx);
        x += w + kGap;
    }
}

void View::renderInfoLine(InfoLineView &v, const ui::InfoLine &line)
{
    const Palette &p = palette();
    show(v.box, !line.text.empty());
    if (line.text.empty()) {
        return;
    }
    const lv_color_t ink = line.banner ? p.onFocus : toneColor(line.tone);
    setFill(v.box, toneColor(line.tone), line.banner);
    const int pad = line.banner ? 6 : 0;
    int x = pad;
    iconSet(v.icon, line.icon, ink);
    if (line.icon != Icon::None) {
        setPos(v.icon, x, 2);
        x += 22;
    }
    setText(v.label, line.text);
    setTextColor(v.label, ink);
    lv_obj_set_x(v.label, x);
    setWidth(v.label, kScreenW - 2 * kMargin - x - pad);
}

// Digits sit in equal cells so a running countdown does not shift sideways.
void View::renderDigits(const std::string &text)
{
    const int digitW = lv_font_get_glyph_width(fontNumerals(), '0', 0) + 1;
    const int colonW = lv_font_get_glyph_width(fontNumerals(), ':', 0);
    int x = kMargin - 2;
    for (int i = 0; i < kDigits; ++i) {
        const bool used = static_cast<std::size_t>(i) < text.size();
        show(digits_[i], used);
        if (!used) {
            continue;
        }
        const char ch[2] = {text[static_cast<std::size_t>(i)], '\0'};
        const int w = ch[0] == ':' ? colonW : digitW;
        setText(digits_[i], ch);
        setPos(digits_[i], x, 65);
        setWidth(digits_[i], w);
        x += w;
    }
    const bool note = !text.empty();
    if (note) {
        // Baselines of the 48 px numerals and the 20 px note line up.
        const int baseline = 65 + lineHeight(fontNumerals()) - fontNumerals()->base_line;
        setPos(bigNote_, x + 8, baseline - (lineHeight(fontBody()) - fontBody()->base_line));
    }
}

void View::renderHome(const ui::HomeModel &h)
{
    const Palette &p = palette();
    // State chip. "ALL OFF" is a quiet outline; every other state is filled.
    const bool quiet = h.chipTone == Tone::Muted || h.chipTone == Tone::Normal;
    setFill(chip_, toneColor(h.chipTone), !quiet);
    setBorder(chip_, p.line, quiet ? 1 : 0);
    const lv_color_t chipInk = quiet ? p.muted : p.onFocus;
    iconSet(chipIcon_, h.chipIcon, chipInk);
    const int chipTextX = h.chipIcon != Icon::None ? 27 : 10;
    setText(chipLabel_, h.chip);
    setTextColor(chipLabel_, chipInk);
    lv_obj_set_x(chipLabel_, chipTextX);
    const int chipW = chipTextX + textWidth(h.chip, fontSmall()) + 10;
    setWidth(chip_, chipW);
    setText(chipNote_, h.chipNote);
    lv_obj_set_x(chipNote_, kMargin + chipW + 8);
    setWidth(chipNote_, kScreenW - kMargin - (kMargin + chipW + 8));

    const bool badge = !h.badge.empty();
    show(badge_, badge);
    setText(badgeLabel_, h.badge);
    setFill(badge_, quiet ? p.surfaceHi : toneColor(h.chipTone));
    const bool titleIcon = !badge && h.titleIcon != Icon::None;
    iconSet(homeTitleIcon_, titleIcon ? h.titleIcon : Icon::None, toneColor(h.titleTone));
    const int titleX = badge || titleIcon ? kMargin + 38 : kMargin;
    const int titleW = kScreenW - kMargin - titleX;
    // With no numerals below, a long headline (a fault) may take two lines.
    // A title that still does not fit drops to the body font before it is
    // cut short.
    const bool twoLines = h.big.empty();
    const int need = textWidth(h.title, fontTitle());
    const lv_font_t *titleFont = need <= (twoLines ? 2 * titleW - 40 : titleW) ? fontTitle() : fontBody();
    const bool wrap = twoLines && textWidth(h.title, titleFont) > titleW;
    setFont(homeTitle_, titleFont);
    lv_label_set_long_mode(homeTitle_, wrap ? LV_LABEL_LONG_MODE_WRAP : LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(homeTitle_, lineHeight(titleFont) * (wrap ? 2 : 1));
    setText(homeTitle_, h.title);
    setTextColor(homeTitle_, toneColor(h.titleTone));
    setPos(homeTitle_, titleX, 33 + (titleFont == fontTitle() ? 0 : 4));
    setWidth(homeTitle_, titleW);

    renderDigits(h.big);
    show(bigNote_, !h.big.empty());
    setText(bigNote_, h.bigNote);

    show(progress_, h.progressPermille >= 0);
    if (h.progressPermille >= 0) {
        const int full = kScreenW - 2 * kMargin;
        setWidth(progressFill_, std::clamp(full * h.progressPermille / 1000, 6, full));
    }
    renderInfoLine(lines_[0], h.lines[0]);
    renderInfoLine(lines_[1], h.lines[1]);
    renderButtons(homeButtons_, h.buttons, h.focus);
}

void View::renderList(const ui::ListModel &list)
{
    const Palette &p = palette();
    const int n = static_cast<int>(list.rows.size());
    constexpr int kBoxW = 306;
    constexpr int kBoxH = kRowH - 2;
    for (int i = 0; i < kRows; ++i) {
        RowView &v = rows_[i];
        const int index = list.top + i;
        show(v.box, index < n);
        if (index >= n) {
            continue;
        }
        const ui::Row &r = list.rows[static_cast<std::size_t>(index)];
        const bool focused = index == list.focus;
        const bool header = r.kind == ui::RowKind::Header;
        setFill(v.box, p.focus, focused);
        show(v.rule, header);

        lv_color_t ink = toneColor(r.tone);
        lv_color_t valueInk = toneColor(r.valueTone);
        if (focused) {
            ink = p.onFocus;
            valueInk = p.onFocus;
        } else if (r.disabled) {
            ink = p.muted;
            valueInk = p.muted;
        }

        if (header) {
            iconSet(v.icon, Icon::None, ink);
            show(v.badge, false);
            setFont(v.label, fontSmall());
            setText(v.label, r.label);
            setTextColor(v.label, p.muted);
            setPos(v.label, 6, kBoxH - lineHeight(fontSmall()) - 6);
            setWidth(v.label, kBoxW - 12);
            show(v.value, false);
            iconSet(v.valueIcon, Icon::None, ink);
            continue;
        }

        const bool badge = !r.badge.empty();
        show(v.badge, badge);
        if (badge) {
            setText(v.badgeLabel, r.badge);
            setFill(v.badge, focused ? p.onFocus : p.surfaceHi);
            setTextColor(v.badgeLabel, focused ? p.focus : (r.disabled ? p.muted : p.text));
        }
        iconSet(v.icon, badge ? Icon::None : r.icon, ink);
        const bool lead = badge || r.icon != Icon::None;
        const int labelX = lead ? 42 : 12;

        // Value and its icon hang on the right edge; the label gets the rest.
        int right = kBoxW - 10;
        iconSet(v.valueIcon, r.valueIcon, valueInk);
        if (r.valueIcon != Icon::None) {
            right -= 18;
            lv_obj_set_x(v.valueIcon, right);
            right -= 5;
        }
        show(v.value, !r.value.empty());
        if (!r.value.empty()) {
            // Label and value share the row. If both do not fit in the body
            // font the value drops to the small font; if that is still too
            // much the value keeps up to 60 % and the label ends in dots.
            const int space = right - labelX;
            const int labelNeed = textWidth(r.label, fontBody());
            const lv_font_t *valueFont = fontBody();
            int valueW = textWidth(r.value, valueFont) + 1;
            if (labelNeed + 10 + valueW > space) {
                valueFont = fontSmall();
                valueW = std::min(textWidth(r.value, valueFont) + 1, space * 6 / 10);
            }
            setFont(v.value, valueFont);
            lv_label_set_long_mode(v.value, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_height(v.value, lineHeight(valueFont));
            setText(v.value, r.value);
            setTextColor(v.value, valueInk);
            setWidth(v.value, valueW);
            right -= valueW;
            setPos(v.value, right, (kBoxH - lineHeight(valueFont)) / 2 + (valueFont == fontSmall() ? 1 : 0));
            right -= 10;
        }
        setFont(v.label, fontBody());
        setText(v.label, r.label);
        setTextColor(v.label, ink);
        setPos(v.label, labelX, (kBoxH - lineHeight(fontBody())) / 2);
        setWidth(v.label, std::max(20, right - labelX));
    }

    const bool scroll = n > kRows;
    show(scrollTrack_, scroll);
    if (scroll) {
        const int trackH = kContentH - 8;
        const int thumbH = std::max(16, trackH * kRows / n);
        const int travel = trackH - thumbH;
        setPos(scrollThumb_, 0, travel * list.top / std::max(1, n - kRows));
        lv_obj_set_height(scrollThumb_, thumbH);
    }
}

void View::renderDetail(const ui::DetailModel &d)
{
    const Palette &p = palette();
    const bool qr = !d.qr.empty();
    show(qrFrame_, qr);
    if (qr && d.qr != qrShown_) {
        lv_qrcode_update(qr_, d.qr.c_str(), static_cast<uint32_t>(d.qr.size()));
        qrShown_ = d.qr;
    }
    iconSet(detailIcon_, qr ? Icon::None : d.icon, toneColor(d.tone));
    const bool icon = !qr && d.icon != Icon::None;
    const int textX = qr ? 168 : (icon ? 56 : 12);
    const int textW = kScreenW - textX - 10;
    setText(detailHeadline_, d.headline);
    setPos(detailHeadline_, textX, qr ? 8 : 10);
    setWidth(detailHeadline_, textW);
    lv_obj_update_layout(detailHeadline_);
    const int headlineBottom = lv_obj_get_y(detailHeadline_) + lv_obj_get_height(detailHeadline_);
    const int bodyX = qr ? textX : 12;
    setText(detailBody_, d.body);
    setTextColor(detailBody_, p.text);
    setPos(detailBody_, bodyX, std::max(headlineBottom + 8, qr ? 0 : 54));
    setWidth(detailBody_, kScreenW - bodyX - 10);
    renderButtons(detailButtons_, d.buttons, d.focus);
}

void View::renderPicker(const ui::PickerModel &m)
{
    const Palette &p = palette();
    setText(pickerCaption_, m.caption);
    setText(pickerHint_, m.hint);
    const int n = std::min<int>(static_cast<int>(m.segments.size()), kSegments);

    if (m.cells) {
        // Weekday cells and an OK button.
        constexpr int kCellW = 33;
        constexpr int kCellH = 44;
        constexpr int kGap = 3;
        constexpr int kOkW = 44;
        const int days = std::max(0, n - 1);
        const int total = days * kCellW + (days > 0 ? (days - 1) * kGap : 0) + 10 + kOkW;
        int x = (kScreenW - total) / 2;
        for (int i = 0; i < kSegments; ++i) {
            show(segBox_[i], i < n);
            if (i >= n) {
                continue;
            }
            const ui::PickerSegment &s = m.segments[static_cast<std::size_t>(i)];
            const bool ok = s.fixed;
            const int w = ok ? kOkW : kCellW;
            if (ok) {
                x += 10 - kGap;
            }
            setPos(segBox_[i], x, 74);
            lv_obj_set_size(segBox_[i], w, kCellH);
            lv_obj_set_style_radius(segBox_[i], 8, LV_PART_MAIN);
            const lv_font_t *cellFont = ok ? fontBody() : fontSmall();
            // LVGL places children inside the border, so the label is moved
            // back by the border width to stay centred.
            const int border = ok ? 0 : (s.cursor ? 3 : (s.on ? 0 : 1));
            setFont(segLabel_[i], cellFont);
            setText(segLabel_[i], s.text);
            setWidth(segLabel_[i], w);
            setPos(segLabel_[i], -border, (kCellH - lineHeight(cellFont)) / 2 - border);
            if (ok) {
                setFill(segBox_[i], s.cursor ? p.focus : p.surfaceHi);
                setBorder(segBox_[i], p.line, 0);
                setTextColor(segLabel_[i], s.cursor ? p.onFocus : p.text);
            } else {
                // Selected days are filled; the cursor is a bright frame.
                setFill(segBox_[i], p.running, s.on);
                setBorder(segBox_[i], s.cursor ? p.focus : p.line, border);
                setTextColor(segLabel_[i], s.on ? p.onFocus : p.muted);
            }
            x += w + kGap;
        }
        iconSet(pickerLeft_, Icon::None, p.muted);
        iconSet(pickerRight_, Icon::None, p.muted);
        show(pickerUnit_, false);
        return;
    }

    // Large numerals. Words that do not fit drop to the title font.
    // A part the knob can change has room around it for the cursor box; a
    // separator has a little air on both sides so it does not lean on the box.
    constexpr int kPadX = 10;
    constexpr int kRowY = 64;
    auto segmentWidth = [](const ui::PickerSegment &s, const lv_font_t *f) {
        if (s.text == " ") {
            return 8;
        }
        return textWidth(s.text, f) + (s.fixed ? 12 : 2 * kPadX);
    };
    const lv_font_t *font = fontNumerals();
    auto measure = [&](const lv_font_t *f) {
        int w = 0;
        for (int i = 0; i < n; ++i) {
            const ui::PickerSegment &s = m.segments[static_cast<std::size_t>(i)];
            w += segmentWidth(s, f);
        }
        return w + (m.unit.empty() ? 0 : textWidth(m.unit, fontTitle()) + 8);
    };
    int total = measure(font);
    if (total > kScreenW - 72) {
        font = fontTitle();
        total = measure(font);
    }
    const int rowH = lineHeight(fontNumerals()) + 8;
    const int boxH = lineHeight(font) + 8;
    const int boxY = kRowY + (rowH - boxH) / 2;
    int x = (kScreenW - total) / 2;
    iconSet(pickerLeft_, Icon::ChevronLeft, p.muted);
    setPos(pickerLeft_, std::max(4, x - 28), kRowY + (rowH - 20) / 2);
    for (int i = 0; i < kSegments; ++i) {
        show(segBox_[i], i < n);
        if (i >= n) {
            continue;
        }
        const ui::PickerSegment &s = m.segments[static_cast<std::size_t>(i)];
        const int w = segmentWidth(s, font);
        setPos(segBox_[i], x, boxY);
        lv_obj_set_size(segBox_[i], w, boxH);
        lv_obj_set_style_radius(segBox_[i], 10, LV_PART_MAIN);
        setFill(segBox_[i], p.focus, s.cursor);
        setBorder(segBox_[i], p.line, 0);
        setFont(segLabel_[i], font);
        setText(segLabel_[i], s.text);
        setTextColor(segLabel_[i], s.cursor ? p.onFocus : p.text);
        setWidth(segLabel_[i], w);
        setPos(segLabel_[i], 0, 4);
        x += w;
    }
    show(pickerUnit_, !m.unit.empty());
    if (!m.unit.empty()) {
        setText(pickerUnit_, m.unit);
        const int baseline = boxY + 4 + lineHeight(font) - font->base_line;
        setPos(pickerUnit_, x + 8, baseline - (lineHeight(fontTitle()) - fontTitle()->base_line));
        x += 8 + textWidth(m.unit, fontTitle());
    }
    iconSet(pickerRight_, Icon::ChevronRight, p.muted);
    setPos(pickerRight_, std::min(kScreenW - 24, x + 8), kRowY + (rowH - 20) / 2);
}

void View::renderToast(const ui::ToastModel &t)
{
    const Palette &p = palette();
    setFill(toast_, t.tone == Tone::Normal || t.tone == Tone::Muted ? p.focus : toneColor(t.tone));
    iconSet(toastIcon_, t.icon, p.onFocus);
    const int x = t.icon != Icon::None ? 34 : kMargin;
    setText(toastLabel_, t.text);
    lv_obj_set_x(toastLabel_, x);
    setWidth(toastLabel_, kScreenW - x - kMargin);
}

} // namespace zf::view

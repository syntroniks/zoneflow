// SPDX-License-Identifier: Apache-2.0
// Draws a zf::ui::Frame with LVGL. The view holds no state of its own beyond
// the LVGL objects: every pixel follows from the Frame, which follows from
// the core's snapshot.
//
// All objects are created once in create(). render() only changes texts,
// colours and visibility, and skips what did not change, so a countdown tick
// redraws the digits and the progress bar, not the screen.
//
// Threading: LVGL is not thread-safe. Call everything from the UI task.
#pragma once

#include <array>

#include "lvgl.h"
#include "zf_ui/model.hpp"

namespace zf::view {

class View {
public:
    // Builds the object tree on the display's active screen.
    void create(lv_display_t *display);
    void render(const ui::Frame &frame);

private:
    static constexpr int kMaxButtons = 4;
    static constexpr int kRows = ui::ListModel::kVisibleRows;
    static constexpr int kDigits = 8;
    static constexpr int kSegments = 9;

    struct ButtonBar {
        lv_obj_t *box[kMaxButtons] = {};
        lv_obj_t *icon[kMaxButtons] = {};
        lv_obj_t *label[kMaxButtons] = {};
    };
    struct RowView {
        lv_obj_t *box = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *badge = nullptr;
        lv_obj_t *badgeLabel = nullptr;
        lv_obj_t *label = nullptr;
        lv_obj_t *value = nullptr;
        lv_obj_t *valueIcon = nullptr;
        lv_obj_t *rule = nullptr;
    };
    struct InfoLineView {
        lv_obj_t *box = nullptr;
        lv_obj_t *icon = nullptr;
        lv_obj_t *label = nullptr;
    };

    void createBar(lv_obj_t *screen);
    void createHome(lv_obj_t *screen);
    void createList(lv_obj_t *screen);
    void createDetail(lv_obj_t *screen);
    void createPicker(lv_obj_t *screen);
    void createToast(lv_obj_t *screen);
    void createButtons(lv_obj_t *parent, ButtonBar &bar);

    void renderBar(const ui::StatusBarModel &bar);
    void renderHome(const ui::HomeModel &home);
    void renderList(const ui::ListModel &list);
    void renderDetail(const ui::DetailModel &detail);
    void renderPicker(const ui::PickerModel &picker);
    void renderToast(const ui::ToastModel &toast);
    void renderButtons(ButtonBar &bar, const std::vector<ui::Button> &buttons, int focus);
    void renderInfoLine(InfoLineView &view, const ui::InfoLine &line);
    void renderDigits(const std::string &text);

    // status bar
    lv_obj_t *bar_ = nullptr;
    lv_obj_t *barBack_ = nullptr;
    lv_obj_t *barTitle_ = nullptr;
    lv_obj_t *barDate_ = nullptr;
    lv_obj_t *barStep_ = nullptr;
    lv_obj_t *barClock_ = nullptr;
    lv_obj_t *barRun_ = nullptr;
    lv_obj_t *barRunIcon_ = nullptr;
    lv_obj_t *barRunLabel_ = nullptr;
    lv_obj_t *barWifi_ = nullptr;
    lv_obj_t *barRain_ = nullptr;
    lv_obj_t *barFault_ = nullptr;

    // home
    lv_obj_t *home_ = nullptr;
    lv_obj_t *chip_ = nullptr;
    lv_obj_t *chipIcon_ = nullptr;
    lv_obj_t *chipLabel_ = nullptr;
    lv_obj_t *chipNote_ = nullptr;
    lv_obj_t *badge_ = nullptr;
    lv_obj_t *badgeLabel_ = nullptr;
    lv_obj_t *homeTitle_ = nullptr;
    lv_obj_t *homeTitleIcon_ = nullptr;
    std::array<lv_obj_t *, kDigits> digits_{};
    lv_obj_t *bigNote_ = nullptr;
    lv_obj_t *progress_ = nullptr;
    lv_obj_t *progressFill_ = nullptr;
    InfoLineView lines_[2];
    ButtonBar homeButtons_;

    // list
    lv_obj_t *list_ = nullptr;
    RowView rows_[kRows];
    lv_obj_t *scrollTrack_ = nullptr;
    lv_obj_t *scrollThumb_ = nullptr;

    // detail
    lv_obj_t *detail_ = nullptr;
    lv_obj_t *detailIcon_ = nullptr;
    lv_obj_t *detailHeadline_ = nullptr;
    lv_obj_t *detailBody_ = nullptr;
    lv_obj_t *qrFrame_ = nullptr;
    lv_obj_t *qr_ = nullptr;
    std::string qrShown_;
    ButtonBar detailButtons_;

    // picker
    lv_obj_t *picker_ = nullptr;
    lv_obj_t *pickerCaption_ = nullptr;
    lv_obj_t *pickerLeft_ = nullptr;
    lv_obj_t *pickerRight_ = nullptr;
    lv_obj_t *segBox_[kSegments] = {};
    lv_obj_t *segLabel_[kSegments] = {};
    lv_obj_t *pickerUnit_ = nullptr;
    lv_obj_t *pickerHint_ = nullptr;

    // toast
    lv_obj_t *toast_ = nullptr;
    lv_obj_t *toastIcon_ = nullptr;
    lv_obj_t *toastLabel_ = nullptr;
};

} // namespace zf::view

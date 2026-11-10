// SPDX-License-Identifier: Apache-2.0
// The icon set. Every icon is drawn here from lines, arcs, rectangles and
// triangles on a 24-unit grid, original to this project. No icon font and no
// bitmap is used, so there is no third-party artwork to license.
#pragma once

#include "lvgl.h"
#include "zf_ui/model.hpp"

namespace zf::view {

// A square object that draws one icon. Hidden while the icon is None.
lv_obj_t *iconCreate(lv_obj_t *parent, int size);
void iconSet(lv_obj_t *obj, ui::Icon icon, lv_color_t color);

} // namespace zf::view

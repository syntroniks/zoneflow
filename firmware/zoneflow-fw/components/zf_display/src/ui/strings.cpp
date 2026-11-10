// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/strings.hpp"

namespace zf::ui {
namespace {

constexpr const char *kEnglish[] = {
#define ZF_UI_X(id, text) text,
    ZF_UI_STRING_TABLE(ZF_UI_X)
#undef ZF_UI_X
};
static_assert(sizeof kEnglish / sizeof kEnglish[0] == static_cast<unsigned>(Str::Count));

} // namespace

const char *tr(Str id)
{
    const auto i = static_cast<unsigned>(id);
    return i < static_cast<unsigned>(Str::Count) ? kEnglish[i] : "";
}

} // namespace zf::ui

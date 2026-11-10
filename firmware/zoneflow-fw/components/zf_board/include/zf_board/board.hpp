// SPDX-License-Identifier: Apache-2.0
// Selects the board definition. Code includes this header, never a specific
// board file. Add a board by adding a header and a Kconfig choice.
#pragma once

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#if !defined(CONFIG_ZF_BOARD_REV_A)
#error "No ZoneFlow board selected (menuconfig: ZoneFlow > Board)"
#endif
#endif

#include "zf_board/zoneflow_rev_a.hpp"

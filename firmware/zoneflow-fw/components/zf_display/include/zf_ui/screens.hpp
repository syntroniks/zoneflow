// SPDX-License-Identifier: Apache-2.0
// The screens. Each factory returns one level of the navigation stack.
// Screen map: docs/design/display-ui.md.
#pragma once

#include <memory>

#include "zf_ui/faults.hpp"
#include "zf_ui/program_edit.hpp"
#include "zf_ui/screen.hpp"

namespace zf::ui {

std::unique_ptr<Screen> makeHomeScreen();
std::unique_ptr<Screen> makeMenuScreen();
std::unique_ptr<Screen> makeManualScreen();
std::unique_ptr<Screen> makeZonesScreen();
std::unique_ptr<Screen> makeZoneScreen(ZoneIndex zone);
std::unique_ptr<Screen> makeProgramsScreen();
std::unique_ptr<Screen> makeProgramScreen(const ProgramEdit &edit);
std::unique_ptr<Screen> makeSensorsScreen();
std::unique_ptr<Screen> makeFaultsScreen();
std::unique_ptr<Screen> makeFaultScreen(const FaultItem &item);
std::unique_ptr<Screen> makeSetupScreen();
std::unique_ptr<Screen> makeNetworkScreen();
std::unique_ptr<Screen> makeProvisioningScreen();
std::unique_ptr<Screen> makeTimeScreen();
std::unique_ptr<Screen> makeTimeZoneScreen();
std::unique_ptr<Screen> makeDisplayScreen();
std::unique_ptr<Screen> makeAboutScreen();
std::unique_ptr<Screen> makeWizardScreen();

} // namespace zf::ui

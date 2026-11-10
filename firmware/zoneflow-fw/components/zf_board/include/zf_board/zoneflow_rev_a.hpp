// SPDX-License-Identifier: Apache-2.0
//
// ZoneFlow revision A board definition.
//
// *** PROVISIONAL ***
// No schematic exists yet (system spec section 3.2: hardware/pinmap.yaml).
// This pin map is a firmware placeholder chosen so the code compiles and the
// rules below hold. The hardware lead owns the real map; when it exists, this
// file is regenerated from, or kept in step with, hardware/pinmap.yaml.
//
// This is the only file in the firmware that names GPIO numbers.
//
// Pin rules applied (ESP32-S3 Series Datasheet v2.2, Espressif):
//  - Strapping pins GPIO0, 3, 45, 46 are not used (section 2, "Strapping pins").
//  - USB D-/D+ GPIO19, 20 stay free for native USB flashing and logs.
//  - Module: ESP32-S3-WROOM-1-N16R8, 16 MB flash and 8 MB octal PSRAM (spec
//    v0.3 asks for PSRAM; sdkconfig.defaults enables it). GPIO26..32 carry the
//    flash, and octal PSRAM takes GPIO33..37; the module brings out GPIO35..37
//    but they are not available. GPIO22..25 do not exist on the chip.
//  - Valve outputs and OE avoid pins with a HIGH-level power-up glitch
//    (Table 2-2: GPIO18, 19, 20). GPIO1..14 and 17 show only a low-level
//    glitch (~60 us), which keeps an active-high output off.
//  - GPIO43 (U0TXD) carries ROM boot messages; not used for outputs.
//  - GPIO47/48 run at 1.8 V on R8V/R16V chip variants (datasheet note 4).
//    The N16R8 is not one of them, so they are used below at 3.3 V; check
//    again if the module changes.
//  - The valve current input is on ADC1 (ADC2 is shared with WiFi).
//
// The firmware assumes external pull-downs on every valve output and on OE so
// outputs stay off while the MCU is in reset or unpowered (spec section 2
// rule 1). That is a hardware requirement, not something firmware can do.
#pragma once

#include <array>
#include <cstdint>

#include "sdkconfig.h"

namespace zf::board {

inline constexpr const char *kBoardName = "zoneflow-rev-a (provisional pin map)";
inline constexpr const char *kBoardShortName = "ZoneFlow rev A"; // for the display's About screen

// ---- GPIO18, 47 and 48 are wanted twice ------------------------------------------
// The over-current trip and the zero-cross input took the last three free
// pins. The display (spec v0.3 section 3.1) needs three more than are left;
// see "pin budget" below. Until hardware/pinmap.yaml settles it, one build
// option decides who gets them. Default: the trip and zero-cross inputs, as
// before, and no display.
#ifdef CONFIG_ZF_DISPLAY_ON_SHARED_PINS
inline constexpr bool kDisplayOnSharedPins = true;
#else
inline constexpr bool kDisplayOnSharedPins = false;
#endif

// ---- module ------------------------------------------------------------------
inline constexpr uint32_t kFlashBytes = 16u * 1024u * 1024u; // keep in step with sdkconfig.defaults
inline constexpr bool kOctalPsram = true;                    // N16R8

// ---- valve outputs: direct GPIO, active high --------------------------------
inline constexpr std::size_t kZoneOutputs = 8; // a 4-zone build fits only Z1..Z4
inline constexpr std::array<int, kZoneOutputs> kZoneGpio = {4, 5, 6, 7, 8, 9, 10, 11}; // Z1..Z8
inline constexpr int kMasterGpio = 12;
inline constexpr bool kOutputActiveHigh = true;

// ---- hardware output enable --------------------------------------------------
// Keep-alive input of the fail-safe gate (spec v0.2 section 2 rule 3): gate
// drive is removed within about 1 s after the toggling stops. A static-level
// variant for other hardware is CONFIG_ZF_OE_MODE_STATIC (menuconfig).
inline constexpr int kOutputEnableGpio = 13;
inline constexpr bool kOutputEnableActiveHigh = true;

// ---- status LED --------------------------------------------------------------
inline constexpr int kStatusLedGpio = 14;
inline constexpr bool kStatusLedActiveHigh = true;

// ---- I2C: RTC, Qwiic expansion (the OLED of spec v0.2 is gone) -------------------
inline constexpr int kI2cSdaGpio = 15; // XTAL_32K_P; no 32 kHz crystal fitted
inline constexpr int kI2cSclGpio = 16; // XTAL_32K_N
inline constexpr uint32_t kI2cFrequencyHz = 400000;
inline constexpr uint8_t kRtcI2cAddress = 0x51; // PCF8563 (NXP datasheet rev 11.1: write A2h / read A3h)

// ---- local controls (zf_display) ---------------------------------------------------
// Spec v0.3 section 3.1: rotary encoder with push switch, Back and
// Manual/Home buttons. Switches close to ground.
inline constexpr int kEncoderAGpio = 38;
inline constexpr int kEncoderBGpio = 39;
inline constexpr int kEncoderPushGpio = 40;
inline constexpr int kButtonBackGpio = 41;
// *** PROVISIONAL, added for the display *** U0RXD: an input during boot.
inline constexpr int kButtonHomeGpio = 44;
inline constexpr bool kControlsActiveLow = true;
// Typical detented encoders give one full quadrature cycle (4 edges) per
// detent. To confirm against the part the hardware lead picks.
inline constexpr int kEncoderCountsPerDetent = 4;
inline constexpr bool kEncoderReversed = false; // true if clockwise counts down

// ---- display: 320x240 IPS TFT, ST7789-class, SPI, no touch (zf_display) ------------------
// *** PROVISIONAL, added for the display *** -1 = no pin.
//  - CS is not driven: the panel is the only device on the bus, CS tied low
//    on the board.
//  - Reset is not driven: tied to the module's EN net; the driver also sends
//    the software reset command.
//  - DC on U0TXD: the ROM prints boot messages there. With no SPI clock
//    running the panel ignores it.
//  - Clock, data and backlight have no pin of their own. With
//    CONFIG_ZF_DISPLAY_ON_SHARED_PINS they take GPIO47, 48 and 18. GPIO18 is
//    one of the pins with a high-level glitch at power-up (see the pin rules
//    above): on a backlight that is a brief flash, not a hazard.
inline constexpr int kLcdSclkGpio = kDisplayOnSharedPins ? 47 : -1;
inline constexpr int kLcdMosiGpio = kDisplayOnSharedPins ? 48 : -1;
inline constexpr int kLcdBacklightGpio = kDisplayOnSharedPins ? 18 : -1;
inline constexpr int kLcdDcGpio = 43;
inline constexpr int kLcdCsGpio = -1;
inline constexpr int kLcdResetGpio = -1;
// zf_display does not start the panel without these.
inline constexpr bool kHasDisplay = kLcdSclkGpio >= 0 && kLcdMosiGpio >= 0 && kLcdBacklightGpio >= 0;
inline constexpr bool kLcdBacklightActiveHigh = true;
inline constexpr uint32_t kLcdBacklightPwmHz = 5000;
// Placeholders until a panel is chosen: clock rate, SPI mode, orientation
// and colour inversion depend on the module and how it is mounted.
inline constexpr uint32_t kLcdSpiHz = 40000000;
inline constexpr int kLcdSpiMode = 0;
inline constexpr int kLcdWidth = 320;
inline constexpr int kLcdHeight = 240;
inline constexpr bool kLcdSwapXy = true; // native scan is 240 x 320 portrait
inline constexpr bool kLcdMirrorX = false;
inline constexpr bool kLcdMirrorY = true;
inline constexpr bool kLcdInvertColors = true; // usual for IPS ST7789 panels

// ---- field inputs ------------------------------------------------------------------
// Dry contacts to ground through the protection network; closed = low.
inline constexpr int kRainGpio = 17;
inline constexpr int kSpareSensorGpio = 42;
inline constexpr int kFlowPulseGpio = 21;
inline constexpr bool kContactClosedLevel = false;
inline constexpr bool kInputsUseInternalPullup = true; // in addition to external pull-ups
// Flow meters reach 200 Hz (spec section 1): 2.5 ms half-period. Glitch filter
// below that.
inline constexpr uint32_t kFlowGlitchFilterNs = 1000;

// 24 VAC present. Assumed: optocoupler pulls the pin low while AC is present.
inline constexpr int kAcPresentGpio = 2;
inline constexpr bool kAcPresentActiveLow = true;

// ---- valve current sense (total of all outputs) --------------------------------------
// Spec v0.2: low-side shunt in the shared triac return, rectifying/averaging
// front end, biased away from the non-linear ADC region near 0 V. Scale and
// bias are placeholders until the hardware lead fixes shunt, gain and bias:
// 1 mV above the bias at the ADC = 1 mA.
inline constexpr int kValveCurrentGpio = 1; // ADC1_CH0
inline constexpr float kValveCurrentMaPerMv = 1.0f;
inline constexpr int kValveCurrentOffsetMv = 0;
inline constexpr int kValveCurrentSamples = 16; // averaged per control tick

// ---- hardware over-current trip (PROVISIONAL: no circuit exists yet) ------------------
// A comparator on the current shunt removes gate drive on a short and latches
// until firmware releases it (spec section 3.1: a fast hardware over-current
// path is preferred). Firmware reads the latch every control tick and pulses
// the clear output once every output is confirmed off.
inline constexpr bool kHasOverCurrentTrip = !kDisplayOnSharedPins;
inline constexpr int kOverCurrentTripGpio = 18;         // input; the high power-up glitch does not matter here
inline constexpr bool kOverCurrentTripActiveLow = true; // assumed open-drain comparator output
inline constexpr int kOverCurrentClearGpio = 47;        // output, pulse to release the latch
inline constexpr bool kOverCurrentClearActiveHigh = true;
inline constexpr uint32_t kOverCurrentClearPulseUs = 10; // assumed; set from the latch's datasheet

// ---- mains zero-cross input (PROVISIONAL: no circuit exists yet) -----------------------
// One pulse per zero crossing of the 24 VAC supply (twice per mains period).
// Used only with driver.zero_cross_switching; firmware ignores a missing or
// implausible signal. The offset is the detector's lead (+) or lag (-)
// relative to the real crossing, to be measured on the first board.
inline constexpr bool kHasZeroCross = !kDisplayOnSharedPins;
inline constexpr int kZeroCrossGpio = 48;
inline constexpr bool kZeroCrossRisingEdge = true;
inline constexpr int32_t kZeroCrossOffsetUs = 0;

// ---- pin budget ---------------------------------------------------------------------------
// Under the rules of this file (forbidden() and octalPsramPin() below) 27
// GPIO are usable: 1, 2, 4..18, 21, 38..44, 47, 48. Counting what the
// firmware wants:
//   valves 8 + master 1 + OE 1 + status LED 1 + I2C 2          = 13
//   rain, spare, flow, 24 VAC present, valve current            =  5
//   over-current trip and clear 2 + zero cross 1                =  3
//   encoder 3 + Back 1 + Manual/Home 1                          =  5
//   display: clock, data, data/command, backlight               =  4
//                                                         total = 30
// That is 3 more than 27, with the panel's CS and reset already left
// undriven. kDisplayOnSharedPins makes a build fit by leaving out either
// the display (default) or the over-current trip and zero-cross inputs.
// Ways out, for the hardware lead: an I/O expander for the switches and the
// LED; a module with quad PSRAM, if GPIO35..37 are then free (to check in
// the module datasheet); or doing without three of these signals.

// ---- compile-time checks ---------------------------------------------------------------
namespace detail {

constexpr bool forbidden(int gpio)
{
    return gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46 || // strapping
           gpio == 19 || gpio == 20 ||                           // USB
           (gpio >= 22 && gpio <= 32) ||                         // not on the chip (22..25) / flash (26..32)
           gpio < 0 || gpio > 48;
}

// Octal PSRAM data lines: GPIO33..37 (35..37 are on module pins but taken).
constexpr bool octalPsramPin(int gpio) { return gpio >= 33 && gpio <= 37; }

constexpr bool highGlitch(int gpio) { return gpio == 18 || gpio == 19 || gpio == 20; }

// A pin of a function that this build does not have counts as not used.
constexpr int usedIf(bool fitted, int gpio) { return fitted ? gpio : -1; }

// Every pin in use. -1 entries (function not fitted, signal not driven) are
// skipped by the checks.
constexpr std::array<int, 30> kAllPins = {
    kZoneGpio[0],
    kZoneGpio[1],
    kZoneGpio[2],
    kZoneGpio[3],
    kZoneGpio[4],
    kZoneGpio[5],
    kZoneGpio[6],
    kZoneGpio[7],
    kMasterGpio,
    kOutputEnableGpio,
    kStatusLedGpio,
    kI2cSdaGpio,
    kI2cSclGpio,
    kEncoderAGpio,
    kEncoderBGpio,
    kEncoderPushGpio,
    kButtonBackGpio,
    kButtonHomeGpio,
    kRainGpio,
    kSpareSensorGpio,
    kFlowPulseGpio,
    kAcPresentGpio,
    kValveCurrentGpio,
    usedIf(kHasOverCurrentTrip, kOverCurrentTripGpio),
    usedIf(kHasOverCurrentTrip, kOverCurrentClearGpio),
    usedIf(kHasZeroCross, kZeroCrossGpio),
    kLcdSclkGpio,
    kLcdMosiGpio,
    kLcdDcGpio,
    kLcdBacklightGpio,
};

constexpr bool allAllowed()
{
    for (int p : kAllPins) {
        if (p >= 0 && forbidden(p)) {
            return false;
        }
    }
    return true;
}

constexpr bool noneOnOctalPsram()
{
    for (int p : kAllPins) {
        if (kOctalPsram && octalPsramPin(p)) {
            return false;
        }
    }
    return true;
}

constexpr bool allUnique()
{
    for (std::size_t i = 0; i < kAllPins.size(); ++i) {
        for (std::size_t j = i + 1; j < kAllPins.size(); ++j) {
            if (kAllPins[i] >= 0 && kAllPins[i] == kAllPins[j]) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool outputsGlitchFree()
{
    for (int p : kZoneGpio) {
        if (highGlitch(p) || p == 43) {
            return false;
        }
    }
    return !highGlitch(kMasterGpio) && !highGlitch(kOutputEnableGpio);
}

} // namespace detail

static_assert(detail::allAllowed(), "board pin on a strapping, USB, flash or non-existent pin");
static_assert(detail::noneOnOctalPsram(), "board pin on GPIO33..37, used by the octal PSRAM of the N16R8 module");
static_assert(detail::allUnique(), "board pin used twice");
static_assert(detail::outputsGlitchFree(), "valve output or OE on a pin with a high-level boot glitch");
static_assert(kValveCurrentGpio >= 1 && kValveCurrentGpio <= 10, "valve current must be on ADC1 (GPIO1..10)");

} // namespace zf::board

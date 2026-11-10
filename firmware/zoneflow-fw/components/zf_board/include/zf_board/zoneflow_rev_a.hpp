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
//  - GPIO26..37 are left to SPI flash and octal PSRAM on WROOM-1 variants.
//  - Valve outputs and OE avoid pins with a HIGH-level power-up glitch
//    (Table 2-2: GPIO18, 19, 20). GPIO1..14 and 17 show only a low-level
//    glitch (~60 us), which keeps an active-high output off.
//  - GPIO43 (U0TXD) carries ROM boot messages; not used for outputs.
//  - GPIO47/48 run at 1.8 V on R8V/R16V chip variants (datasheet note 4);
//    left spare.
//  - The valve current input is on ADC1 (ADC2 is shared with WiFi).
//
// The firmware assumes external pull-downs on every valve output and on OE so
// outputs stay off while the MCU is in reset or unpowered (spec section 2
// rule 1). That is a hardware requirement, not something firmware can do.
#pragma once

#include <array>
#include <cstdint>

namespace zf::board {

inline constexpr const char *kBoardName = "zoneflow-rev-a (provisional pin map)";

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

// ---- I2C: OLED (SSD1306/SH1106 128x64), RTC, Qwiic expansion -------------------
inline constexpr int kI2cSdaGpio = 15; // XTAL_32K_P; no 32 kHz crystal fitted
inline constexpr int kI2cSclGpio = 16; // XTAL_32K_N
inline constexpr uint32_t kI2cFrequencyHz = 400000;
inline constexpr uint8_t kRtcI2cAddress = 0x51;  // PCF8563 (NXP datasheet rev 11.1: write A2h / read A3h)
inline constexpr uint8_t kOledI2cAddress = 0x3C; // typical SSD1306/SH1106 module, to confirm

// ---- local controls (used by the display/UI module) -----------------------------
inline constexpr int kEncoderAGpio = 38;
inline constexpr int kEncoderBGpio = 39;
inline constexpr int kEncoderPushGpio = 40;
inline constexpr int kButtonGpio = 41; // Back/Manual
inline constexpr bool kControlsActiveLow = true;

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
inline constexpr bool kHasOverCurrentTrip = true;
inline constexpr int kOverCurrentTripGpio = 18;         // input; the high power-up glitch does not matter here
inline constexpr bool kOverCurrentTripActiveLow = true; // assumed open-drain comparator output
inline constexpr int kOverCurrentClearGpio = 47;        // output, pulse to release the latch
inline constexpr bool kOverCurrentClearActiveHigh = true;
inline constexpr uint32_t kOverCurrentClearPulseUs = 10; // assumed; set from the latch's datasheet

// ---- spare GPIO, not assigned: 48 -------------------------------------------------------

// ---- compile-time checks ---------------------------------------------------------------
namespace detail {

constexpr bool forbidden(int gpio)
{
    return gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46 || // strapping
           gpio == 19 || gpio == 20 ||                           // USB
           (gpio >= 22 && gpio <= 37) ||                         // not bonded (22..25) / flash, PSRAM
           gpio < 0 || gpio > 48;
}

constexpr bool highGlitch(int gpio) { return gpio == 18 || gpio == 19 || gpio == 20; }

constexpr std::array<int, 24> kAllPins = {
    kZoneGpio[0],     kZoneGpio[1],   kZoneGpio[2],   kZoneGpio[3],      kZoneGpio[4],         kZoneGpio[5],
    kZoneGpio[6],     kZoneGpio[7],   kMasterGpio,    kOutputEnableGpio, kStatusLedGpio,       kI2cSdaGpio,
    kI2cSclGpio,      kEncoderAGpio,  kEncoderBGpio,  kEncoderPushGpio,  kButtonGpio,          kRainGpio,
    kSpareSensorGpio, kFlowPulseGpio, kAcPresentGpio, kValveCurrentGpio, kOverCurrentTripGpio, kOverCurrentClearGpio,
};

constexpr bool allAllowed()
{
    for (int p : kAllPins) {
        if (forbidden(p)) {
            return false;
        }
    }
    return true;
}

constexpr bool allUnique()
{
    for (std::size_t i = 0; i < kAllPins.size(); ++i) {
        for (std::size_t j = i + 1; j < kAllPins.size(); ++j) {
            if (kAllPins[i] == kAllPins[j]) {
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

static_assert(detail::allAllowed(), "board pin on a strapping, USB or flash/PSRAM pin");
static_assert(detail::allUnique(), "board pin used twice");
static_assert(detail::outputsGlitchFree(), "valve output or OE on a pin with a high-level boot glitch");
static_assert(kValveCurrentGpio >= 1 && kValveCurrentGpio <= 10, "valve current must be on ADC1 (GPIO1..10)");

} // namespace zf::board

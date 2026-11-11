// SPDX-License-Identifier: Apache-2.0
//
// ZoneFlow revision A board definition.
//
// Every GPIO number, I2C address and expander bit comes from
// hardware/pinmap.yaml through zf_board/pinmap_generated.hpp, which each
// build generates (tools/gen_board_header.py). This file gives the signals
// the names the firmware uses, adds the constants the pin map does not carry
// (scales, timings, display settings, each with its source) and checks the
// pin rules at compile time. It names no GPIO number itself.
//
// Sources: docs/design/hardware-design.md (sections 7 to 15 and 19) and
// hardware/pinmap.yaml. Nothing here has been measured: no board exists.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "zf_board/pinmap_generated.hpp"

namespace zf::board {

namespace pm = pinmap;

inline constexpr const char *kBoardName = "zoneflow-rev-a";
inline constexpr const char *kBoardShortName = "ZoneFlow rev A"; // for the display's About screen

// ---- module ------------------------------------------------------------------
inline constexpr uint32_t kFlashBytes = 16u * 1024u * 1024u; // keep in step with sdkconfig.defaults
inline constexpr bool kOctalPsram = true;                    // N16R8

// ---- output stage: keep-alive, read-backs, over-current latch -------------------
// Hardware design 8 and 19.4. Valve outputs are expander bits, not GPIOs.
inline constexpr int kKeepAliveGpio = pm::kKeepaliveGpio;         // plain GPIO output, idle low
inline constexpr int kDriveEnableReadbackGpio = pm::kDrvEnRbGpio; // A_RB: high = V_DRV on (gate drive); pull-down
inline constexpr int kRelayOffReadbackGpio = pm::kRlyBRbGpio;     // B_RB: high = relay transistor Q2 off; no pull
inline constexpr int kOverCurrentGpio = pm::kOcFaultNGpio;        // low while the latch is set; no pull
static_assert(!pm::kOcFaultNActiveHigh, "OC_FAULT_N is active low");
static_assert(pm::kDrvEnRbActiveHigh && pm::kRlyBRbActiveHigh, "read-backs are active high");

// ---- I2C0: output expander and RTC -------------------------------------------------
inline constexpr int kI2cPort = pm::kI2c0BusPort;
inline constexpr int kI2cSdaGpio = pm::kI2c0BusSdaGpio;
inline constexpr int kI2cSclGpio = pm::kI2c0BusSclGpio;
inline constexpr uint32_t kI2cFrequencyHz = pm::kI2c0BusSpeedHz;
inline constexpr uint8_t kExpanderI2cAddress = pm::kIoExpanderAddress; // TCA9539, A0 = A1 = GND
inline constexpr uint8_t kRtcI2cAddress = pm::kRtcAddress;             // PCF8563
// Bounded I2C in the safety path. A 3-byte TCA9539 transaction at 400 kHz
// takes about 0.1 ms; 5 ms leaves room for the bus lock held by an RTC
// transaction on another task (also short) and stays far below the 50 ms
// control period. The driver adds at most 50 ms for a bus clear
// (ESP-IDF i2c_master.c, I2C_CLR_BUS_TIMEOUT_MS).
inline constexpr int kExpanderI2cTimeoutMs = 5;

// Expander bits, from the pin map (port, bit).
struct ExpanderBits {
    std::array<pm::ExpanderBit, 8> zone;
    pm::ExpanderBit master;
    pm::ExpanderBit relay;
    pm::ExpanderBit variant8ZoneN;
    pm::ExpanderBit sensorFaultN;
};
inline constexpr ExpanderBits kExpanderBits = {
    {pm::kZone1Bit, pm::kZone2Bit, pm::kZone3Bit, pm::kZone4Bit, pm::kZone5Bit, pm::kZone6Bit, pm::kZone7Bit,
     pm::kZone8Bit},
    pm::kMasterBit,
    pm::kComRelayBit,
    pm::kVariant8zoneNBit,
    pm::kSensPwrFaultNBit,
};
inline constexpr std::size_t kZoneOutputs = 8; // the 4-zone build reads its variant strap at start-up
inline constexpr int kMaxOutputsOn = pm::kMaxOutputsOn;

// ---- valve current: two biased AC channels on ADC1 ----------------------------------
// Hardware design 10 and 19.2; scales also in the pin map notes.
inline constexpr int kIsenseHiGpio = pm::kIsenseHiGpio;
inline constexpr int kIsenseLoGpio = pm::kIsenseLoGpio;
inline constexpr int kIsenseHiAdcChannel = pm::kIsenseHiAdcChannel;
inline constexpr int kIsenseLoAdcChannel = pm::kIsenseLoAdcChannel;
inline constexpr int kAdcAttenuationDb = pm::kAdcAttenuationDb; // 0 to 1600 mV
inline constexpr float kIsenseHiMaPerMv = 1.503f;               // 49.9k / 7.5k gain, 0.1 ohm shunt
inline constexpr float kIsenseLoMaPerMv = 7.50f;                // 10k / 7.5k gain
inline constexpr int kIsenseHiMinMv = 60;                       // below or above: clipped, use ISENSE_LO
inline constexpr int kIsenseHiMaxMv = 1500;
inline constexpr int kIsenseBiasMv = 790; // nominal window mean (VB through the amplifiers)
// Sampling: 5 kS/s per channel, 10 kS/s for the pair. 100 ms = 500
// samples per channel: five cycles at 50 Hz, six at 60 Hz. The design asks
// for at least 2 kS/s per channel.
inline constexpr uint32_t kIsenseSampleHzPerChannel = 5000;
inline constexpr uint32_t kIsenseWindowMs = 100;
static_assert(kIsenseSampleHzPerChannel >= 2000, "hardware design 19.2: at least 2000 samples per second per channel");
static_assert(kAdcAttenuationDb == 6, "the front end is designed for 6 dB attenuation (0 to 1600 mV)");

// ---- AC presence and zero-cross: square wave on AC_ZC -----------------------------------
// Hardware design 11 and 19.9, at 24 VAC (both grow at lower supply voltage).
inline constexpr int kZeroCrossGpio = pm::kAcZcGpio;
inline constexpr int32_t kZeroCrossRisingLagUs = 300;  // rising edge after the upward crossing
inline constexpr int32_t kZeroCrossFallingLeadUs = 90; // falling edge before the downward crossing

// ---- sensors ----------------------------------------------------------------------------
inline constexpr int kRainGpio = pm::kSensRainGpio;
inline constexpr int kFlowPulseGpio = pm::kSensFlowGpio;
inline constexpr int kSpareSensorGpio = pm::kSensAuxGpio;
inline constexpr bool kContactClosedLevel = pm::kSensRainActiveHigh; // closed reads low
inline constexpr bool kInputsUseInternalPullup = false;              // external pull-ups to V5, clamped to 3V3 (13.2)
inline constexpr uint32_t kFlowGlitchFilterNs = 1000; // flow passes 200 Hz; the RC filter is about 0.3 ms
// Sensor supply switch U11. GPIO45 is a strapping pin (flash voltage): left
// alone until the application runs, then driven high. Never a pull-up.
inline constexpr int kSensorSupplyGpio = pm::kSensPwrEnGpio;
static_assert(pm::kSensPwrEnActiveHigh, "SENS_PWR_EN is active high");

// ---- status LED -------------------------------------------------------------------------
inline constexpr int kStatusLedGpio = pm::kStatusLedGpio;
inline constexpr bool kStatusLedActiveHigh = pm::kStatusLedActiveHigh;

// ---- display: HS20HS072RX, 240 x 320, ST7789-class, SPI2 (hardware design 15) -----------------
inline constexpr int kLcdSclkGpio = pm::kTftSclkGpio;
inline constexpr int kLcdMosiGpio = pm::kTftMosiGpio;
inline constexpr int kLcdCsGpio = pm::kTftCsGpio;
inline constexpr int kLcdDcGpio = pm::kTftDcGpio;
inline constexpr int kLcdResetGpio = pm::kTftRstGpio;
inline constexpr int kLcdBacklightGpio = pm::kTftBlGpio;
inline constexpr bool kLcdBacklightActiveHigh = pm::kTftBlActiveHigh;
// 1 to 20 kHz (hardware design 15.2 and rule 9).
inline constexpr uint32_t kLcdBacklightPwmHz = 5000;
static_assert(kLcdBacklightPwmHz >= 1000 && kLcdBacklightPwmHz <= 20000, "backlight PWM must be 1 to 20 kHz");
// ST7789V datasheet v1.3: 66 ns minimum serial write cycle, so 15 MHz is
// the guaranteed clock (hardware design 15.3). A full frame takes 82 ms.
inline constexpr uint32_t kLcdSpiHz = 15000000;
inline constexpr uint32_t kLcdSpiMaxHz = 15000000;
static_assert(kLcdSpiHz <= kLcdSpiMaxHz, "display SPI clock above the ST7789V guaranteed 15 MHz");
inline constexpr int kLcdSpiMode = 0;
inline constexpr int kLcdWidth = 320; // mounted landscape (hardware design 19.3, question 8)
inline constexpr int kLcdHeight = 240;
// Orientation and inversion: the panel is mounted landscape with the tail
// towards the encoder. Mirror flags and inversion are a guess until a panel
// has been seen (its controller variant is not certain, 15.1).
inline constexpr bool kLcdSwapXy = true; // native scan is 240 x 320 portrait
inline constexpr bool kLcdMirrorX = false;
inline constexpr bool kLcdMirrorY = true;
inline constexpr bool kLcdInvertColors = true; // usual for IPS ST7789 panels

// ---- controls: encoder and two buttons, all active low (hardware design 15.4) ----------------
inline constexpr int kEncoderAGpio = pm::kEncAGpio;
inline constexpr int kEncoderBGpio = pm::kEncBGpio;
inline constexpr int kEncoderPushGpio = pm::kEncSwGpio;
inline constexpr int kButtonBackGpio = pm::kBtnBackGpio;
inline constexpr int kButtonHomeGpio = pm::kBtnHomeGpio;
inline constexpr bool kControlsActiveLow = !pm::kEncAActiveHigh;
// EC11J1525402: 30 detents and 15 pulses per turn. 15 x 4 quadrature edges
// over 30 detents = 2 counts per detent.
inline constexpr int kEncoderCountsPerDetent = 2;
inline constexpr bool kEncoderReversed = false; // true if clockwise counts down; check on the first board

// ---- compile-time checks -------------------------------------------------------------------------
namespace detail {

constexpr bool strapping(int gpio) { return gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46; }

// A strapping pin may carry only a function that cannot disturb its strap
// (pin map rules): BOOT on 0, VIN_MON on 3 (divider holds it low), the
// sensor supply enable on 45 (R86 holds it low), the status LED on 46.
constexpr bool strapAllowed(const pm::GpioEntry &e)
{
    const std::string_view s = e.signal;
    switch (e.gpio) {
    case 0:
        return s == "BOOT";
    case 3:
        return s == "VIN_MON";
    case 45:
        return s == "SENS_PWR_EN";
    case 46:
        return s == "STATUS_LED";
    default:
        return true;
    }
}

constexpr bool pinRulesHold()
{
    for (const pm::GpioEntry &e : pm::kGpio) {
        if (e.gpio >= 22 && e.gpio <= 37) {
            return false; // not on the chip, flash, or octal PSRAM
        }
        if (strapping(e.gpio) && !strapAllowed(e)) {
            return false;
        }
    }
    return true;
}

// Whether the pin map lists `gpio` with this direction. By value: GCC
// rejects pointer comparisons in constant expressions under the sanitizers.
constexpr bool hasDirection(int gpio, pm::Direction d)
{
    for (const pm::GpioEntry &e : pm::kGpio) {
        if (e.gpio == gpio) {
            return e.direction == d;
        }
    }
    return false;
}

constexpr bool isInput(int gpio) { return hasDirection(gpio, pm::Direction::In); }
constexpr bool isOutput(int gpio) { return hasDirection(gpio, pm::Direction::Out); }

constexpr bool expanderBitsDistinct()
{
    std::array<pm::ExpanderBit, 12> all = {
        kExpanderBits.zone[0], kExpanderBits.zone[1], kExpanderBits.zone[2],       kExpanderBits.zone[3],
        kExpanderBits.zone[4], kExpanderBits.zone[5], kExpanderBits.zone[6],       kExpanderBits.zone[7],
        kExpanderBits.master,  kExpanderBits.relay,   kExpanderBits.variant8ZoneN, kExpanderBits.sensorFaultN};
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            if (all[i].port == all[j].port && all[i].bit == all[j].bit) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool valveBitsAreActiveLowOutputs()
{
    for (const pm::ExpanderBit &b : kExpanderBits.zone) {
        if (!b.output || !b.activeLow) {
            return false;
        }
    }
    return kExpanderBits.master.output && kExpanderBits.master.activeLow && kExpanderBits.relay.output &&
           !kExpanderBits.relay.activeLow && !kExpanderBits.variant8ZoneN.output && !kExpanderBits.sensorFaultN.output;
}

} // namespace detail

static_assert(detail::pinRulesHold(), "pin map uses GPIO22..37 or a strapping pin for a function that can disturb it");
static_assert(detail::isOutput(kKeepAliveGpio), "the keep-alive pin must be an output");
static_assert(detail::isInput(kDriveEnableReadbackGpio) && detail::isInput(kRelayOffReadbackGpio),
              "GPIO7 and GPIO42 read back the detectors and must stay inputs (rule 12)");
static_assert(detail::isInput(kOverCurrentGpio) && detail::isInput(kZeroCrossGpio), "latch and zero-cross are inputs");
static_assert(detail::expanderBitsDistinct(), "expander bit used twice");
static_assert(detail::valveBitsAreActiveLowOutputs(), "valve bits are active-low outputs, the relay active high");
static_assert(kIsenseHiGpio >= 1 && kIsenseHiGpio <= 10 && kIsenseLoGpio >= 1 && kIsenseLoGpio <= 10,
              "current sense must be on ADC1 (GPIO1..10)");
static_assert(kMaxOutputsOn == 3, "two zones plus the master (system spec section 1)");

} // namespace zf::board

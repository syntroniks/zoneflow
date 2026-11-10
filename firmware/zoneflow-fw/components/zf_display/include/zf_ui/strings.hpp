// SPDX-License-Identifier: Apache-2.0
// Every string the display shows, in one table. English only for now; a
// translation adds a second table with the same ids. Format strings use
// printf conversions and are marked in the comment column of the table.
//
// Wording rules (docs/design/display-ui.md): say what happened, then what to
// check. No codes, no jargon, no exclamation marks.
#pragma once

#include <cstdint>

// X(id, text)
#define ZF_UI_STRING_TABLE(X)                                                                                          \
    /* ---- general ---- */                                                                                            \
    X(AppName, "ZoneFlow")                                                                                             \
    X(On, "On")                                                                                                        \
    X(Off, "Off")                                                                                                      \
    X(Cancel, "Cancel")                                                                                                \
    X(Ok, "OK")                                                                                                        \
    X(Save, "Save")                                                                                                    \
    X(Discard, "Discard")                                                                                              \
    X(Next, "Next")                                                                                                    \
    X(Later, "Later")                                                                                                  \
    X(Done, "Done")                                                                                                    \
    X(Never, "Never")                                                                                                  \
    X(None, "None")                                                                                                    \
    X(NotMeasured, "No data")                                                                                          \
    X(UnitMin, "min")                                                                                                  \
    X(UnitPercent, "%")                                                                                                \
    X(UnitDays, "days")                                                                                                \
    X(FmtMinutes, "%d min")                                                                                            \
    X(FmtSeconds, "%d s")                                                                                              \
    X(FmtHours, "%d h")                                                                                                \
    X(FmtHoursMinutes, "%d h %02d min")                                                                                \
    X(FmtMinSec, "%d:%02d min")                                                                                        \
    X(FmtDays, "%d days")                                                                                              \
    X(OneDay, "1 day")                                                                                                 \
    X(FmtPercent, "%d %%")                                                                                             \
    X(FmtMilliamp, "%d mA")                                                                                            \
    X(FmtFlow, "%d.%d L/min")                                                                                          \
    X(FmtLitres, "%ld L")                                                                                              \
    X(FmtDbm, "%d dBm")                                                                                                \
    X(FmtZone, "Zone %d")                                                                                              \
    X(Today, "Today")                                                                                                  \
    X(Tomorrow, "Tomorrow")                                                                                            \
    X(EveryDay, "Every day")                                                                                           \
    X(Sun, "Sun")                                                                                                      \
    X(Mon, "Mon")                                                                                                      \
    X(Tue, "Tue")                                                                                                      \
    X(Wed, "Wed")                                                                                                      \
    X(Thu, "Thu")                                                                                                      \
    X(Fri, "Fri")                                                                                                      \
    X(Sat, "Sat")                                                                                                      \
    X(Su, "Su")                                                                                                        \
    X(Mo, "Mo")                                                                                                        \
    X(Tu, "Tu")                                                                                                        \
    X(We, "We")                                                                                                        \
    X(Th, "Th")                                                                                                        \
    X(Fr, "Fr")                                                                                                        \
    X(Sa, "Sa")                                                                                                        \
    X(Jan, "Jan")                                                                                                      \
    X(Feb, "Feb")                                                                                                      \
    X(Mar, "Mar")                                                                                                      \
    X(Apr, "Apr")                                                                                                      \
    X(May, "May")                                                                                                      \
    X(Jun, "Jun")                                                                                                      \
    X(Jul, "Jul")                                                                                                      \
    X(Aug, "Aug")                                                                                                      \
    X(Sep, "Sep")                                                                                                      \
    X(Oct, "Oct")                                                                                                      \
    X(Nov, "Nov")                                                                                                      \
    X(Dec, "Dec")                                                                                                      \
    /* ---- picker hints ---- */                                                                                       \
    X(HintTurnPress, "Turn to change, press to confirm")                                                               \
    X(HintTurnNext, "Turn to change, press for next")                                                                  \
    X(HintWeekdays, "Turn to move, press to switch a day")                                                             \
    /* ---- home ---- */                                                                                               \
    X(ChipWatering, "WATERING")                                                                                        \
    X(ChipPaused, "PAUSED")                                                                                            \
    X(ChipStarting, "STARTING")                                                                                        \
    X(ChipAllOff, "ALL OFF")                                                                                           \
    X(ChipNoData, "NO DATA")                                                                                           \
    X(HomeLeft, "left")                                                                                                \
    X(HomeNextRun, "Next run")                                                                                         \
    X(HomeNoSchedule, "No run planned")                                                                                \
    X(HomeNoScheduleHint, "Add a program in the menu")                                                                 \
    X(HomeManual, "Manual")                                                                                            \
    X(HomeThen, "Then %s, %s")          /* zone label, duration */                                                     \
    X(HomeThenMore, "Then %s, %s  +%d") /* zone label, duration, more entries */                                       \
    X(HomeLastZone, "Last zone in the queue")                                                                          \
    X(HomeProgramSummary, "%s, %s") /* program name, total duration */                                                 \
    X(HomeRainDelayUntil, "Rain delay until %s")                                                                       \
    X(HomeSensorWet, "Rain sensor wet: programs on hold")                                                              \
    X(HomeSensorDry, "Rain sensor dry")                                                                                \
    X(HomeClockNotSet, "Programs are off until it is set")                                                             \
    X(HomeMasterOpen, "Master valve open")                                                                             \
    X(HomeNoData, "Controller not responding")                                                                         \
    X(HomeNoDataHint, "Valve state unknown")                                                                           \
    X(HomeMaintenance, "Updating: watering is off")                                                                    \
    X(HomeFaultMore, "%s  +%d")     /* fault title, more faults */                                                     \
    X(HomeNextRunAt, "Next run %s") /* day and time */                                                                 \
    X(BtnStop, "Stop")                                                                                                 \
    X(BtnPause, "Pause")                                                                                               \
    X(BtnResume, "Resume")                                                                                             \
    X(BtnSkip, "Skip")                                                                                                 \
    X(BtnMenu, "Menu")                                                                                                 \
    X(BtnManual, "Manual")                                                                                             \
    X(BtnRain, "Rain")                                                                                                 \
    X(BtnFaults, "Faults")                                                                                             \
    X(ToastStopped, "All valves off")                                                                                  \
    X(ToastBusy, "Busy. Try again.")                                                                                   \
    X(ToastSaved, "Saved")                                                                                             \
    X(ToastNotSaved, "Not saved: %s")            /* reason */                                                          \
    X(ToastZoneStarted, "%s: %s")                /* zone label, duration */                                            \
    X(ToastZoneQueued, "%s queued")              /* zone label */                                                      \
    X(ToastProgramStarted, "%s started")         /* program name */                                                    \
    X(ToastProgramSkipped, "%s skipped: %s")     /* program name, reason */                                            \
    X(ToastProgramMissed, "%s missed its start") /* program name */                                                    \
    X(ToastFaultsCleared, "Faults cleared")                                                                            \
    X(SkipRainDelay, "rain delay")                                                                                     \
    X(SkipSensor, "rain sensor")                                                                                       \
    X(SkipNoAc, "no 24 V AC")                                                                                          \
    X(SkipMaintenance, "update running")                                                                               \
    X(SkipAlreadyQueued, "already running")                                                                            \
    X(SkipQueueFull, "queue full")                                                                                     \
    X(SkipNothing, "nothing to run")                                                                                   \
    X(SkipMasterLocked, "master valve locked")                                                                         \
    X(ToastNotStarted, "Not started: %s") /* reason */                                                                 \
    X(ToastRunDropped, "%s skipped: %s")  /* zone label, reason */                                                     \
    X(ToastPauseExpired, "Paused too long: stopped")                                                                   \
    X(WhyNotFound, "not found")                                                                                        \
    X(WhyBlocked, "valves are blocked")                                                                                \
    X(WhyZoneLocked, "zone locked by a fault")                                                                         \
    X(WhyZoneResting, "zone is resting")                                                                               \
    X(WhyZoneDisabled, "zone disabled")                                                                                \
    X(WhyUnknown, "could not start")                                                                                   \
    /* ---- menu ---- */                                                                                               \
    X(MenuTitle, "Menu")                                                                                               \
    X(MenuManual, "Manual run")                                                                                        \
    X(MenuZones, "Zones")                                                                                              \
    X(MenuPrograms, "Programs")                                                                                        \
    X(MenuRainDelay, "Rain delay")                                                                                     \
    X(MenuSensors, "Sensors and flow")                                                                                 \
    X(MenuFaults, "Faults")                                                                                            \
    X(MenuSetup, "Setup")                                                                                              \
    /* ---- manual run ---- */                                                                                         \
    X(HdrZones, "ZONES")                                                                                               \
    X(HdrPrograms, "PROGRAMS")                                                                                         \
    X(ManualNoAc, "No 24 V AC supply")                                                                                 \
    X(ManualMaintenance, "Update running, watering off")                                                               \
    X(ManualMasterLocked, "Master valve locked")                                                                       \
    X(PickRunFor, "Run %s for")                        /* zone label */                                                \
    X(ConfirmRunProgram, "Run %s now?")                /* program name */                                              \
    X(ConfirmRunProgramBody, "%d zones, %s in total.") /* count, duration */                                           \
    X(RunNow, "Run now")                                                                                               \
    /* ---- zones ---- */                                                                                              \
    X(ZoneOff, "Off")                                                                                                  \
    X(ZoneRunning, "Running")                                                                                          \
    X(ZoneStarting, "Starting")                                                                                        \
    X(ZoneDisabled, "Disabled")                                                                                        \
    X(ZoneFault, "Fault")                                                                                              \
    X(ZoneLocked, "Locked")                                                                                            \
    X(ZoneResting, "Resting")                                                                                          \
    X(ZoneEnabled, "Enabled")                                                                                          \
    X(ZoneTest, "Test run")                                                                                            \
    X(ZoneState, "State")                                                                                              \
    X(ZoneLastCurrent, "Last current")                                                                                 \
    X(ZoneLearned, "Learned current")                                                                                  \
    X(ZoneMaxRun, "Longest run")                                                                                       \
    X(ZoneClear, "Clear faults")                                                                                       \
    X(ZoneRenameHint, "Rename in the web interface")                                                                   \
    /* ---- programs ---- */                                                                                           \
    X(ProgSeasonal, "Seasonal adjust")                                                                                 \
    X(ProgAdd, "Add program")                                                                                          \
    X(ProgFull, "No room for another program")                                                                         \
    X(ProgDefaultName, "Program %d")                                                                                   \
    X(ProgEnabled, "Enabled")                                                                                          \
    X(HdrStartTimes, "START TIMES")                                                                                    \
    X(ProgStartN, "Start %d")                                                                                          \
    X(ProgAddStart, "Add start time")                                                                                  \
    X(HdrDays, "DAYS")                                                                                                 \
    X(ProgRule, "Runs on")                                                                                             \
    X(RuleWeekdays, "Weekdays")                                                                                        \
    X(RuleInterval, "Interval")                                                                                        \
    X(RuleOdd, "Odd dates")                                                                                            \
    X(RuleEven, "Even dates")                                                                                          \
    X(ProgWeekdays, "Days")                                                                                            \
    X(ProgEvery, "Every")                                                                                              \
    X(HdrRunTimes, "ZONE RUN TIMES")                                                                                   \
    X(HdrOptions, "OPTIONS")                                                                                           \
    X(ProgObey, "Rain sensor")                                                                                         \
    X(ProgObeyYes, "Obey")                                                                                             \
    X(ProgObeyNo, "Ignore")                                                                                            \
    X(ProgDelete, "Delete program")                                                                                    \
    X(ConfirmDelete, "Delete %s?") /* program name */                                                                  \
    X(ConfirmDeleteBody, "The program and its start times are removed.")                                               \
    X(Delete, "Delete")                                                                                                \
    X(ConfirmSave, "Save changes to %s?") /* program name */                                                           \
    X(ConfirmSaveBody, "Discard returns the program to how it was.")                                                   \
    X(ProgWarnNoStart, "No start time yet")                                                                            \
    X(ProgWarnNoZones, "No zone has a run time")                                                                       \
    X(ProgWarnNoDays, "No day selected")                                                                               \
    X(ProgNextNone, "Not planned")                                                                                     \
    X(PickStart, "Start time %d")                                                                                      \
    X(PickRunTime, "Run time, %s") /* zone label */                                                                    \
    X(PickInterval, "Run every")                                                                                       \
    /* ---- rain delay ---- */                                                                                         \
    X(RainCaption, "Hold all programs for")                                                                            \
    X(ToastRainSet, "Rain delay until %s")                                                                             \
    X(ToastRainOff, "Rain delay off")                                                                                  \
    X(RainNeedsClock, "Set the clock first")                                                                           \
    /* ---- sensors ---- */                                                                                            \
    X(SensRain, "Rain sensor")                                                                                         \
    X(SensSpare, "Spare input")                                                                                        \
    X(SensAc, "24 V AC supply")                                                                                        \
    X(SensFlow, "Flow")                                                                                                \
    X(SensTotal, "Water since on")                                                                                     \
    X(SensCurrent, "Valve current")                                                                                    \
    X(SensMaster, "Master valve")                                                                                      \
    X(Dry, "Dry")                                                                                                      \
    X(Wet, "Wet")                                                                                                      \
    X(NotUsed, "Not used")                                                                                             \
    X(Present, "Present")                                                                                              \
    X(Missing, "Missing")                                                                                              \
    X(Open, "Open")                                                                                                    \
    X(Closed, "Closed")                                                                                                \
    X(Active, "Active")                                                                                                \
    X(Inactive, "Inactive")                                                                                            \
    /* ---- faults ---- */                                                                                             \
    X(FaultsNone, "No faults")                                                                                         \
    X(FaultsClearAll, "Clear all faults")                                                                              \
    X(FaultMaster, "Master valve")                                                                                     \
    X(FaultOpenTitle, "%s: no current") /* zone label */                                                               \
    X(FaultOpenBody, "The valve drew no current, so it did not open. "                                                 \
                     "Check: valve wire may be disconnected or cut, or the solenoid has failed.")                      \
    X(FaultOverTitle, "%s: current too high") /* zone label */                                                         \
    X(FaultOverBody, "The zone was switched off and locked. "                                                          \
                     "Check: damaged solenoid, or two valves on one terminal.")                                        \
    X(FaultShortTitle, "%s: short circuit") /* zone label */                                                           \
    X(FaultShortBody, "All valves were switched off and this zone is locked. "                                         \
                      "Check: the zone wire may touch the common wire.")                                               \
    X(FaultIdleTitle, "Current with all valves off")                                                                   \
    X(FaultIdleBody, "A valve output may be stuck on. "                                                                \
                     "If water is flowing, unplug the 24 V supply. The board needs service.")                          \
    X(FaultAcTitle, "No 24 V AC supply")                                                                               \
    X(FaultAcBody, "Valves cannot open. Watering resumes when power returns. "                                         \
                   "Check: transformer plugged in, fuse, supply wires.")                                               \
    X(FaultFlowTitle, "Water flows with all valves off")                                                               \
    X(FaultFlowBody, "The flow meter counts water although nothing is running. "                                       \
                     "Check: a leak, or a valve that does not close.")                                                 \
    X(FaultConfigTitle, "Settings could not be read")                                                                  \
    X(FaultConfigBody, "The stored settings were rejected and defaults are in use. "                                   \
                       "Set up zones and programs again, or restore a backup.")                                        \
    X(FaultTimeTitle, "Clock not set")                                                                                 \
    X(FaultTimeBody, "Programs do not start until the time is known. "                                                 \
                     "Connect WiFi, or set the time under Setup, Time.")                                               \
    X(FaultStorageTitle, "Settings could not be saved")                                                                \
    X(FaultStorageBody, "Writing to memory failed. Try again. "                                                        \
                        "If it keeps failing, the board needs service.")                                               \
    X(FaultMasterLockTitle, "Master valve locked")                                                                     \
    X(FaultMasterLockBody, "The master valve failed several times in a row, so no zone can run. "                      \
                           "Check: master valve wiring and solenoid. Then clear the fault.")                           \
    X(FaultSenseTitle, "Valve current not measured")                                                                   \
    X(FaultSenseBody, "The board gets no current reading. Zones still run, but a broken or shorted "                   \
                      "valve is not noticed. If it stays, the board needs service.")                                   \
    X(FaultTripTitle, "Protection tripped")                                                                            \
    X(FaultTripBody, "A short made the board switch all valves off for a moment. The other zones carry on. A "         \
                     "zone with three trips in a day stays off. Check: valve wires touching each other or the "        \
                     "common wire. Then clear the fault.")                                                             \
    X(FaultDetectorTitle, "Safety circuit failed its test")                                                            \
    X(FaultDetectorBody, "The circuit that turns the valves off if the controller stops failed its self-test. "        \
                         "Nothing runs. Repair the board, then confirm it here.")                                      \
    X(FaultExpanderTitle, "Valve driver not responding")                                                               \
    X(FaultExpanderBody, "The chip that drives the valves did not answer or did not read back what was "               \
                         "written. All valves were switched off. Clear the fault to try again. If it comes "           \
                         "back, the board needs service.")                                                             \
    X(FaultWeldedTitle, "Valve relay stuck closed")                                                                    \
    X(FaultWeldedBody, "Current flowed although the valve relay should have been open: a welded relay contact "        \
                       "or a bridged jumper. Nothing runs. The board needs service.")                                  \
    X(FaultOutputShortTitle, "Valve output stuck on")                                                                  \
    X(FaultOutputShortBody, "Current flowed with every valve off: a valve output on the board is shorted. The "        \
                            "relay was opened and nothing runs. The board needs service.")                             \
    X(FaultLatchTitle, "Protection did not reset")                                                                     \
    X(FaultLatchBody, "The short-circuit protection stayed tripped after the valves were switched off. Nothing "       \
                      "runs. Clear the fault to try again. If it comes back, the board needs service.")                \
    X(FaultSupplyTitle, "Sensor supply overloaded")                                                                    \
    X(FaultSupplyBody, "The 5 V terminal for sensors is overloaded or too hot. Check: sensor wiring and how "          \
                       "much current the sensors draw (100 mA at most).")                                              \
    X(FaultDcTitle, "%s: valve current lopsided") /* zone label */                                                     \
    X(FaultDcBody, "The valve current flowed in one direction only, so the output that drives it is failing. "         \
                   "The zone was switched off. The board needs service.")                                              \
    X(FaultMainsTitle, "Mains signal wrong")                                                                           \
    X(FaultMainsBody, "The 24 V AC timing signal is not at mains frequency. Zero-cross switching and current "         \
                      "readings may be off. The board needs service.")                                                 \
    X(FaultStep, "Step %d: %s") /* self-test step, check name */                                                       \
    X(FaultRepaired, "Repaired")                                                                                       \
    X(FaultRepairedAsk, "Was the board repaired?")                                                                     \
    X(FaultRepairedAskBody, "Confirm only after the safety circuit was repaired and checked. The full "                \
                            "self-test runs again before any valve opens.")                                            \
    X(FaultRepairedYes, "Yes, repaired")                                                                               \
    X(ToastRepairConfirmed, "Repair confirmed")                                                                        \
    X(SkipHardware, "board fault")                                                                                     \
    X(SensBoard, "Valve board")                                                                                        \
    X(BoardReady, "Ready")                                                                                             \
    X(BoardTesting, "Testing")                                                                                         \
    X(BoardIdle, "Idle")                                                                                               \
    X(BoardFault, "Fault")                                                                                             \
    X(BoardNoAc, "No 24 V AC")                                                                                         \
    X(FaultClear, "Clear")                                                                                             \
    X(FaultClearsItself, "Clears by itself")                                                                           \
    /* ---- setup ---- */                                                                                              \
    X(SetupNetwork, "Network")                                                                                         \
    X(SetupTime, "Time")                                                                                               \
    X(SetupDisplay, "Display")                                                                                         \
    X(SetupZoneCount, "Zones in use")                                                                                  \
    X(SetupAbout, "About")                                                                                             \
    X(SetupWizard, "Run setup again")                                                                                  \
    X(SetupReset, "Factory reset")                                                                                     \
    X(NetWifi, "WiFi")                                                                                                 \
    X(NetSsid, "Network")                                                                                              \
    X(NetIp, "IP address")                                                                                             \
    X(NetSignal, "Signal")                                                                                             \
    X(NetMac, "MAC")                                                                                                   \
    X(NetHostname, "Name")                                                                                             \
    X(NetSetup, "Set up WiFi")                                                                                         \
    X(LinkUnavailable, "Not in this build")                                                                            \
    X(LinkNotProvisioned, "Not set up")                                                                                \
    X(LinkProvisioning, "Waiting for app")                                                                             \
    X(LinkConnecting, "Connecting")                                                                                    \
    X(LinkConnected, "Connected")                                                                                      \
    X(LinkDisconnected, "No connection")                                                                               \
    X(ProvHeadline, "Scan with the app")                                                                               \
    X(ProvBody, "Use the ESP BLE Provisioning app on a phone.")                                                        \
    X(ProvNotRunningHeadline, "WiFi setup is not running")                                                             \
    X(ProvNotRunningBody, "To join another network, start WiFi setup from the web interface, "                         \
                          "or do a factory reset.")                                                                    \
    X(ToastErasing, "Erasing, then restarting")                                                                        \
    X(ProvUnavailableHeadline, "WiFi setup is not available")                                                          \
    X(ProvUnavailableBody, "This firmware build has no network module. The controller works without it.")              \
    X(TimeZoneRow, "Time zone")                                                                                        \
    X(TimeSet, "Set the clock")                                                                                        \
    X(TimeDate, "Date")                                                                                                \
    X(TimeOffset, "UTC offset")                                                                                        \
    X(TimeSource, "Clock set by")                                                                                      \
    X(TimeSynced, "Network")                                                                                           \
    X(TimeRtc, "Backup clock")                                                                                         \
    X(TimeUnset, "Not set")                                                                                            \
    X(PickDate, "Date")                                                                                                \
    X(PickTime, "Time")                                                                                                \
    X(TimeCustomZone, "Custom")                                                                                        \
    X(DispBrightness, "Brightness")                                                                                    \
    X(DispDim, "Dim after")                                                                                            \
    X(DispOff, "Screen off after")                                                                                     \
    X(AboutFirmware, "Firmware")                                                                                       \
    X(AboutIdf, "ESP-IDF")                                                                                             \
    X(AboutGraphics, "LVGL")                                                                                           \
    X(AboutBoard, "Board")                                                                                             \
    X(AboutDevice, "Device name")                                                                                      \
    X(AboutLicence, "Licence")                                                                                         \
    X(AboutLicenceValue, "Apache-2.0")                                                                                 \
    X(ResetHeadline, "Erase all settings?")                                                                            \
    X(ResetBody, "Zones, programs, WiFi and display settings are erased. The controller restarts.")                    \
    X(ResetConfirm, "Erase")                                                                                           \
    X(PickZoneCount, "How many zones are wired?")                                                                      \
    /* ---- setup wizard ---- */                                                                                       \
    X(WizWelcome, "Welcome")                                                                                           \
    X(WizWelcomeBody, "Turn the knob to choose.\nPress the knob to select.\nThe left button goes back.")               \
    X(WizStart, "Start")                                                                                               \
    X(WizDoneHeadline, "Ready")                                                                                        \
    X(WizDoneBody, "Add programs under Menu, Programs. The Manual button runs a zone right away.")

namespace zf::ui {

enum class Str : uint16_t {
#define ZF_UI_X(id, text) id,
    ZF_UI_STRING_TABLE(ZF_UI_X)
#undef ZF_UI_X
        Count,
};

// Never returns null.
const char *tr(Str id);

} // namespace zf::ui

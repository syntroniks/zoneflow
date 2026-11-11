// SPDX-License-Identifier: Apache-2.0
// Stable lowercase names used in logs and, later, in the REST API and MQTT.
#include "zf/events/command.hpp"
#include "zf/events/event.hpp"
#include "zf/fault_text.hpp"

namespace zf {

const char *toString(FaultCode f)
{
    switch (f) {
    case FaultCode::None:
        return "none";
    case FaultCode::ZoneOpenCircuit:
        return "zone_open_circuit";
    case FaultCode::ZoneOverCurrent:
        return "zone_over_current";
    case FaultCode::ShortCircuit:
        return "short_circuit";
    case FaultCode::CurrentWhileIdle:
        return "current_while_idle";
    case FaultCode::AcLost:
        return "ac_lost";
    case FaultCode::FlowWithoutZones:
        return "flow_without_zones";
    case FaultCode::ConfigInvalid:
        return "config_invalid";
    case FaultCode::TimeInvalid:
        return "time_invalid";
    case FaultCode::StorageError:
        return "storage_error";
    case FaultCode::MasterLockedOut:
        return "master_locked_out";
    case FaultCode::CurrentSenseUnavailable:
        return "current_sense_unavailable";
    case FaultCode::OverCurrentTrip:
        return "over_current_trip";
    case FaultCode::KeepAliveDetector:
        return "keepalive_detector";
    case FaultCode::OutputExpander:
        return "output_expander";
    case FaultCode::RelayWelded:
        return "relay_welded";
    case FaultCode::OutputShorted:
        return "output_shorted";
    case FaultCode::TripLatchStuck:
        return "trip_latch_stuck";
    case FaultCode::SensorSupply:
        return "sensor_supply";
    case FaultCode::ZoneDcCurrent:
        return "zone_dc_current";
    case FaultCode::MainsSignal:
        return "mains_signal";
    case FaultCode::Count:
        break;
    }
    return "?";
}

const char *toString(TimeStatus s)
{
    switch (s) {
    case TimeStatus::Unset:
        return "unset";
    case TimeStatus::FromRtc:
        return "rtc";
    case TimeStatus::Synced:
        return "synced";
    }
    return "?";
}

const char *toString(EventType t)
{
    switch (t) {
    case EventType::ZoneChanged:
        return "zone_changed";
    case EventType::MasterChanged:
        return "master_changed";
    case EventType::QueueChanged:
        return "queue_changed";
    case EventType::ProgramStarted:
        return "program_started";
    case EventType::ProgramSkipped:
        return "program_skipped";
    case EventType::ProgramMissed:
        return "program_missed";
    case EventType::SensorChanged:
        return "sensor_changed";
    case EventType::FaultRaised:
        return "fault_raised";
    case EventType::FaultCleared:
        return "fault_cleared";
    case EventType::RainDelayChanged:
        return "rain_delay_changed";
    case EventType::ConfigApplied:
        return "config_applied";
    case EventType::TimeStatusChanged:
        return "time_status_changed";
    case EventType::MaintenanceChanged:
        return "maintenance_changed";
    case EventType::CommandRejected:
        return "command_rejected";
    case EventType::RunDropped:
        return "run_dropped";
    case EventType::RunEnded:
        return "run_ended";
    case EventType::PauseExpired:
        return "pause_expired";
    case EventType::Count:
        break;
    }
    return "?";
}

const char *toString(CommandType t)
{
    switch (t) {
    case CommandType::RunZone:
        return "run_zone";
    case CommandType::RunProgram:
        return "run_program";
    case CommandType::StopAll:
        return "stop_all";
    case CommandType::Pause:
        return "pause";
    case CommandType::Resume:
        return "resume";
    case CommandType::Skip:
        return "skip";
    case CommandType::SetRainDelay:
        return "set_rain_delay";
    case CommandType::ClearFaults:
        return "clear_faults";
    case CommandType::SetMaintenance:
        return "set_maintenance";
    case CommandType::ReloadConfig:
        return "reload_config";
    case CommandType::AcknowledgeRepair:
        return "acknowledge_repair";
    }
    return "?";
}

FaultCode faultCodeFor(HwCheck c)
{
    switch (c) {
    case HwCheck::IdleDriveEnabled:
    case HwCheck::IdleRelayOn:
    case HwCheck::IdleExpanderAnswers:
    case HwCheck::PinHighDriveEnabled:
    case HwCheck::PinHighRelayOn:
    case HwCheck::StartDriveTimeout:
    case HwCheck::StartRelayTimeout:
    case HwCheck::RunDriveLost:
    case HwCheck::RunRelayLost:
    case HwCheck::StopDriveRelease:
    case HwCheck::StopRelayRelease:
    case HwCheck::StopExpanderAnswers:
        return FaultCode::KeepAliveDetector;
    case HwCheck::RelayWelded:
        return FaultCode::RelayWelded;
    case HwCheck::OutputShorted:
        return FaultCode::OutputShorted;
    case HwCheck::LatchStuck:
        return FaultCode::TripLatchStuck;
    case HwCheck::StartNoAcknowledge:
    case HwCheck::ExpanderInputs:
    case HwCheck::ExpanderOutputs:
    case HwCheck::ExpanderConfig:
    case HwCheck::RelayNoReadback:
    case HwCheck::RunReadback:
    case HwCheck::EndNoAcknowledge:
    case HwCheck::BusError:
    case HwCheck::TooManyOutputs:
        return FaultCode::OutputExpander;
    case HwCheck::None:
    case HwCheck::Count:
        break;
    }
    return FaultCode::None;
}

} // namespace zf

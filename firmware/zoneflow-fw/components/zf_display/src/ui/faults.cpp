// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/faults.hpp"

#include "zf_ui/format.hpp"

namespace zf::ui {
namespace {

bool zoneType(FaultCode c)
{
    return c == FaultCode::ZoneOpenCircuit || c == FaultCode::ZoneOverCurrent || c == FaultCode::ShortCircuit ||
           c == FaultCode::ZoneDcCurrent;
}

bool stageFault(FaultCode c)
{
    return c == FaultCode::KeepAliveDetector || c == FaultCode::OutputExpander || c == FaultCode::RelayWelded ||
           c == FaultCode::OutputShorted || c == FaultCode::TripLatchStuck;
}

} // namespace

std::vector<FaultItem> listFaults(const SystemState &state)
{
    std::vector<FaultItem> out;
    uint32_t carriedByZone = 0;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        const FaultCode c = state.zones[z].fault;
        if (c != FaultCode::None) {
            out.push_back({c, static_cast<ZoneIndex>(z)});
            carriedByZone |= 1u << static_cast<unsigned>(c);
        }
    }
    for (unsigned bit = 1; bit < static_cast<unsigned>(FaultCode::Count); ++bit) {
        if ((state.activeFaults & (1u << bit)) == 0) {
            continue;
        }
        const auto code = static_cast<FaultCode>(bit);
        if (zoneType(code) && (carriedByZone & (1u << bit)) != 0) {
            continue;
        }
        FaultItem item{code, kNoZone};
        if (stageFault(code)) {
            item.check = state.hardware.check;
            item.repairNeeded = state.hardware.repairNeeded;
        }
        out.push_back(item);
    }
    return out;
}

std::string faultTitle(const FaultItem &item)
{
    const std::string who =
        item.zone == kNoZone ? std::string(tr(Str::FaultMaster)) : fmt(Str::FmtZone, static_cast<int>(item.zone) + 1);
    switch (item.code) {
    case FaultCode::ZoneOpenCircuit:
        return fmt(Str::FaultOpenTitle, who.c_str());
    case FaultCode::ZoneOverCurrent:
        return fmt(Str::FaultOverTitle, who.c_str());
    case FaultCode::ShortCircuit:
        return fmt(Str::FaultShortTitle, who.c_str());
    case FaultCode::CurrentWhileIdle:
        return tr(Str::FaultIdleTitle);
    case FaultCode::AcLost:
        return tr(Str::FaultAcTitle);
    case FaultCode::FlowWithoutZones:
        return tr(Str::FaultFlowTitle);
    case FaultCode::ConfigInvalid:
        return tr(Str::FaultConfigTitle);
    case FaultCode::TimeInvalid:
        return tr(Str::FaultTimeTitle);
    case FaultCode::StorageError:
        return tr(Str::FaultStorageTitle);
    case FaultCode::MasterLockedOut:
        return tr(Str::FaultMasterLockTitle);
    case FaultCode::CurrentSenseUnavailable:
        return tr(Str::FaultSenseTitle);
    case FaultCode::OverCurrentTrip:
        return tr(Str::FaultTripTitle);
    case FaultCode::KeepAliveDetector:
        return tr(Str::FaultDetectorTitle);
    case FaultCode::OutputExpander:
        return tr(Str::FaultExpanderTitle);
    case FaultCode::RelayWelded:
        return tr(Str::FaultWeldedTitle);
    case FaultCode::OutputShorted:
        return tr(Str::FaultOutputShortTitle);
    case FaultCode::TripLatchStuck:
        return tr(Str::FaultLatchTitle);
    case FaultCode::SensorSupply:
        return tr(Str::FaultSupplyTitle);
    case FaultCode::ZoneDcCurrent:
        return fmt(Str::FaultDcTitle, who.c_str());
    case FaultCode::MainsSignal:
        return tr(Str::FaultMainsTitle);
    case FaultCode::None:
    case FaultCode::Count:
        break;
    }
    return "";
}

Str faultBody(FaultCode code)
{
    switch (code) {
    case FaultCode::ZoneOpenCircuit:
        return Str::FaultOpenBody;
    case FaultCode::ZoneOverCurrent:
        return Str::FaultOverBody;
    case FaultCode::ShortCircuit:
        return Str::FaultShortBody;
    case FaultCode::CurrentWhileIdle:
        return Str::FaultIdleBody;
    case FaultCode::AcLost:
        return Str::FaultAcBody;
    case FaultCode::FlowWithoutZones:
        return Str::FaultFlowBody;
    case FaultCode::ConfigInvalid:
        return Str::FaultConfigBody;
    case FaultCode::TimeInvalid:
        return Str::FaultTimeBody;
    case FaultCode::StorageError:
        return Str::FaultStorageBody;
    case FaultCode::MasterLockedOut:
        return Str::FaultMasterLockBody;
    case FaultCode::CurrentSenseUnavailable:
        return Str::FaultSenseBody;
    case FaultCode::OverCurrentTrip:
        return Str::FaultTripBody;
    case FaultCode::KeepAliveDetector:
        return Str::FaultDetectorBody;
    case FaultCode::OutputExpander:
        return Str::FaultExpanderBody;
    case FaultCode::RelayWelded:
        return Str::FaultWeldedBody;
    case FaultCode::OutputShorted:
        return Str::FaultOutputShortBody;
    case FaultCode::TripLatchStuck:
        return Str::FaultLatchBody;
    case FaultCode::SensorSupply:
        return Str::FaultSupplyBody;
    case FaultCode::ZoneDcCurrent:
        return Str::FaultDcBody;
    case FaultCode::MainsSignal:
        return Str::FaultMainsBody;
    case FaultCode::None:
    case FaultCode::Count:
        break;
    }
    return Str::Count;
}

bool faultClearsItself(FaultCode code)
{
    return code == FaultCode::AcLost || code == FaultCode::TimeInvalid || code == FaultCode::CurrentSenseUnavailable ||
           code == FaultCode::SensorSupply || code == FaultCode::MainsSignal;
}

Tone faultTone(FaultCode code)
{
    return code == FaultCode::TimeInvalid || code == FaultCode::CurrentSenseUnavailable ||
                   code == FaultCode::SensorSupply || code == FaultCode::MainsSignal
               ? Tone::Warn
               : Tone::Fault;
}

std::string faultStepText(const FaultItem &item)
{
    const uint8_t step = hwCheckStep(item.check);
    if (item.check == HwCheck::None || step == 0 || step > 9) {
        return {};
    }
    return fmt(Str::FaultStep, static_cast<int>(step), toString(item.check));
}

} // namespace zf::ui

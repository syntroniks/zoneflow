// SPDX-License-Identifier: Apache-2.0
// zf_ota: firmware update with rollback (spec 4.1). Design:
// docs/design/firmware-architecture.md, section 14.
//
//  - Upload: zf_web streams the request body through upload*().
//  - Pull: startPull() downloads an https:// URL (GitHub release assets)
//    with the ESP-IDF certificate bundle, on its own task.
//  - Both refuse while watering unless forced, then put the controller in
//    maintenance (all outputs off, queue cleared, starts blocked) and wait
//    until the state snapshot shows every output off before writing flash.
//  - The new image must pass a self-test after boot (control loop running,
//    web server up, stored config accepted) within 60 s, or it is marked
//    invalid and the bootloader returns to the previous image.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "zf/app/services.hpp"
#include "zf_ota/ota_logic.hpp"

namespace zf::ota {

esp_err_t start(const AppServices &services);

// Called by app_main after every module started. If the running image is
// new (pending verification) this starts the self-test task, which confirms
// the image or rolls back. Otherwise it does nothing. Safe on every boot.
esp_err_t markRunningAppValid();

// zf_web reports that its server runs (one of the self-test conditions).
void notifyWebStarted();

OtaStatus status();

struct Slots {
    std::string running;
    std::string next;
    std::string runningState;
    std::string otherVersion;
};
Slots slots();

// Streamed upload. begin() applies the safety gate and waits for the
// outputs to report off (up to 5 s). finish() verifies the image, sets it as
// boot image and schedules a restart in 1 s. Call abort() if the client
// goes away; it ends maintenance.
OtaError uploadBegin(std::size_t totalBytes, bool force);
OtaError uploadWrite(const uint8_t *data, std::size_t len);
OtaError uploadFinish();
void uploadAbort();

// Web UI image for the "www" partition, streamed like a firmware upload.
// The caller unmounts the file system first and mounts it again after
// finish/abort. Same safety gate (flash erases stall both cores, so the
// outputs are off while writing). Each 4 KB sector is erased just before it
// is written so no single erase is long.
OtaError assetsBegin(std::size_t totalBytes, bool force);
OtaError assetsWrite(const uint8_t *data, std::size_t len);
OtaError assetsFinish();
void assetsAbort();

// Starts the pull task. Returns at once; progress is in status().
OtaError startPull(std::string_view url, bool force);

} // namespace zf::ota

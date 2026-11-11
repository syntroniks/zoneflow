// SPDX-License-Identifier: Apache-2.0
// This board's detector release times (hardware design 19.7, the limits of
// step 9) in key/value blob storage: NVS on the standalone firmware.
//
// One key, kKey, 14 bytes: magic "ZFKR", version, tA and tB in ms, CRC-32 of
// what precedes it, all little-endian. A record that fails any check reads
// as "not stored", and the board learns its times again. The key is not one
// of the configuration keys, so a factory reset keeps it: the times belong
// to the board, not to its settings.
#pragma once

#include "zf/config/config_store.hpp"
#include "zf/hal.hpp"

namespace zf::hw {

class BlobReferenceStore final : public IDetectorReferenceStore {
public:
    static constexpr const char *kKey = "ka_ref";
    static constexpr uint32_t kMagic = 0x524B465Au; // "ZFKR"
    static constexpr uint16_t kVersion = 1;

    explicit BlobReferenceStore(IBlobStorage &storage) : storage_(storage) {}
    bool load(DetectorReference &out) override;
    bool save(const DetectorReference &r) override;
    bool clear() override;

    static std::string encode(const DetectorReference &r);
    static bool decode(std::string_view data, DetectorReference &out);

private:
    IBlobStorage &storage_;
};

} // namespace zf::hw

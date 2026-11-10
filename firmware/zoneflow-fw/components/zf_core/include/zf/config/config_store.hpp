// SPDX-License-Identifier: Apache-2.0
// Owner of the current configuration and its persistence.
//
// Threads: get(), revision() and toJson() from any task. update() and
// updateFromJson() from any task except the control task:
// they write flash. Writers are serialised by a writer-only mutex that is
// held across the storage write; readers never take that mutex, so a slow
// flash write cannot block the control task. After a successful update the
// caller posts CommandType::ReloadConfig.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "zf/config/config.hpp"
#include "zf/config/config_json.hpp"

namespace zf {

// Key/value blob storage (NVS on the device, memory in tests).
class IBlobStorage {
public:
    virtual ~IBlobStorage() = default;
    // Returns false if the key does not exist or cannot be read.
    virtual bool read(const char *key, std::string &out) = 0;
    virtual bool write(const char *key, std::string_view data) = 0;
    virtual bool erase(const char *key) = 0;
};

class ConfigStore {
public:
    static constexpr const char *kConfigKey = "config";
    static constexpr const char *kRuntimeKey = "runtime";

    enum class LoadStatus : uint8_t {
        Loaded,         // stored config, current schema
        Migrated,       // stored config migrated and written back
        NewerSchema,    // stored by newer firmware; parsed best-effort, not written back
        Defaults,       // nothing stored
        InvalidDefaults // stored config rejected; defaults in use, stored copy kept
    };

    struct LoadResult {
        LoadStatus status = LoadStatus::Defaults;
        ValidationResult validation;
    };

    explicit ConfigStore(IBlobStorage &storage) : storage_(storage), config_(Config::defaults()) {}

    LoadResult load();

    Config get() const;
    uint32_t revision() const { return revision_.load(std::memory_order_acquire); }
    std::string toJson(bool pretty = false) const;

    // Validates, persists, then replaces the in-memory copy. Nothing changes
    // if validation or the storage write fails.
    ValidationResult update(const Config &cfg);
    // As update(). Rejects a document with a newer schema than this firmware
    // (path "schema"): writing it back would drop the fields it does not know.
    ValidationResult updateFromJson(std::string_view json);

    // Erases the stored config and returns to defaults.
    bool factoryReset();

    static const char *toString(LoadStatus s);

private:
    IBlobStorage &storage_;
    mutable std::mutex mutex_; // guards config_; held only for copies
    std::mutex writerMutex_;   // serialises writers across the flash write
    Config config_;
    std::atomic<uint32_t> revision_{1};
};

} // namespace zf
